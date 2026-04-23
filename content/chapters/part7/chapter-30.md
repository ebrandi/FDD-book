---
title: "Virtualisation and Containerization"
description: "Driver development for virtualized and containerized environments"
partNumber: 7
partName: "Mastery Topics: Special Scenarios and Edge Cases"
chapter: 30
lastUpdated: "2026-04-19"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "TBD"
estimatedReadTime: 270
---

# Virtualisation and Containerization

## Introduction

You arrive at Chapter 30 with a new habit of mind. Chapter 29 taught you how to shape a driver so that it absorbs variation in hardware, bus, and architecture without collapsing into a maze of conditionals. You have learned to push the parts that change into small, backend-specific files and to let the core remain clean. You have met the idea that a driver is not written for a single machine; it is written for a family of machines that share a programming model. That lesson carries a great deal of weight in this chapter, because the environment around your driver can now change in a way that is more radical than anything Chapter 29 considered. The machine itself may not be real.

This chapter is about what happens when the hardware under your driver is not a physical card plugged into a physical slot, but a software simulation presented to a guest operating system by a hypervisor; or when the driver is asked to attach inside a jail that sees only part of the device tree; or when the network stack it cooperates with is one of several stacks running side by side inside a single kernel. Each of these situations is a departure from the "one kernel, one machine, one device tree" mental model that the earlier chapters built. Each one changes what your driver may assume, what it may do, and what a user of your driver may safely expect from it.

The word "virtualisation" can mean several different things depending on who is speaking. To a system administrator running a cloud fleet, it means virtual machines with full kernels of their own. To a FreeBSD user, it often means `jail(8)` and its descendants, which isolate processes and filesystems without giving each one a kernel. To a driver author, it means both, and more. It means paravirtual devices like VirtIO that are explicitly designed to be easy for a guest to drive. It means emulated devices that a hypervisor presents to a guest as if they were physical hardware. It means passthrough devices where a real piece of hardware is handed to a guest more or less directly, with the hypervisor stepping aside as much as it safely can. It means jails whose view of `devfs` is trimmed by a ruleset so that the containerised processes inside cannot see every device the host can see. It means VNET jails that own an entire network stack of their own, complete with an `ifnet` that was loaned to them by the host.

If that list already feels like a lot, take a breath. The chapter will introduce each of these pieces one at a time, with enough real FreeBSD grounding that you will be able to close the book and open the source tree and find the relevant files with your own hands. Nothing in this chapter is impossible to learn; it is a set of distinct but related ideas that share a common thread. The thread is this: a driver stops being the single most important piece of software in its particular hardware path. Above it sits a hypervisor, a jail framework, or a container runtime; beside it sit other guests or jails that share the host; below it sits a piece of silicon that the driver no longer owns exclusively. Writing drivers for that world is not harder than writing drivers for bare metal, but it is different in ways that are easy to miss if you have not been shown them.

Two distinct directions run through the chapter. The first direction is about **writing guest drivers**: code that runs inside a virtual machine and talks to devices the hypervisor presents. You will spend the bulk of your time in this direction, because it is where most of the novel programming happens. VirtIO is the canonical example, and we will return to it often. The second direction is about **cooperating with FreeBSD's own virtualisation infrastructure** from the host side: understanding how your driver attaches inside a jail, how it behaves when the host moves an interface into a VNET jail, how it handles a user inside a jail trying to call an `ioctl` that only root on the host should be allowed to call, and how to test it all without ruining a running host. This direction is less about exotic new APIs and more about discipline, privilege boundaries, and a careful mental model of what is visible from where.

The chapter does not try to teach you how to write a hypervisor, how to implement a new VirtIO transport, or how to build a container runtime from scratch. Those are large topics with their own books. What it teaches is how a driver author should think about, prepare for, and work with virtualised and containerised environments, so that the drivers you write make sense everywhere they are loaded. By the end of the chapter, you will recognise a VirtIO guest driver when you see one, you will know how to detect whether your driver is running in a virtual machine, you will be able to explain what `devfs_ruleset(8)` does and why it matters for device exposure, you will be able to reason about the differences between VNET jails and ordinary jails for a network driver, and you will have written a tiny VirtIO guest driver of your own against a FreeBSD `bhyve(8)` backend.

Before we begin, a word about the tone of what follows. Some of the subjects in this chapter, especially hypervisor internals and jail security boundaries, have acquired a reputation for being exotic. A driver author who has never looked under the hood can feel intimidated by their jargon. You should not. The code is FreeBSD code; the APIs are FreeBSD APIs; the mindset is the same mindset you have been building since Chapter 1. We will move slowly, and we will keep coming back to real files you can open. Let us begin.

## Reader Guidance: How to Use This Chapter

This chapter sits in a different place in the learning progression from Chapter 29. Chapter 29 was about how your driver is organised on disk. Chapter 30 is about what the world around your driver looks like at runtime. That difference matters for how you should read and practise. The patterns of Chapter 29 can be absorbed by reading carefully and typing along. The patterns of this chapter land more firmly if you also boot a virtual machine, create a jail, and watch the driver behave inside them. Plan accordingly.

If you choose the **reading-only path**, plan for roughly two to three focused hours. At the end you will understand the conceptual map: what paravirtual, emulated, and passthrough devices are; how VirtIO uses shared rings and feature negotiation; how jails and VNET isolate devices and network stacks; what `rctl(8)` controls from the driver's point of view. You will not yet have the reflexes of a driver author who has debugged a virtqueue mismatch at three in the morning, and that is fine. The reading pass is a legitimate first encounter, and the material has enough depth that a second pass with labs later on will extract much more value from it.

If you choose the **reading-plus-labs path**, plan for six to ten hours spread across two or three sessions. You will install `bhyve(8)` on your lab machine, boot a FreeBSD 14.3 guest on top of it with VirtIO devices, write a small VirtIO pseudo-device driver called `vtedu`, and observe it attach, service requests from the host-side device, and detach cleanly. You will also create a simple jail, attach the driver to it under different `devfs` rulesets, and watch what happens when the driver exposes a device inside the jail. The labs are structured so that each one stands on its own, leaves you with a working system, and reinforces a specific concept from the main text.

If you choose the **reading-plus-labs-plus-challenges path**, plan for a long weekend or a handful of evenings. The challenges push the baseline labs into more realistic territory: extending `vtedu` to accept multiple virtqueues, writing a small tool that probes the guest's hypervisor through `vm.guest` and adapts behaviour, building a VNET jail and moving a tap interface into it, and writing a short report on how a driver that exports an `ioctl` surface should decide which ioctls should be accessible from inside a jail. Each challenge is invitational rather than mandatory, and each one is sized to be completable without a second weekend.

A note on the lab environment. You will continue to use the throwaway FreeBSD 14.3 machine you established in earlier chapters. That machine will act as the **host** in this chapter. On top of it you will run `bhyve(8)` guests, which are smaller FreeBSD installations managed by your host. You will also create a few jails directly on the host. This nesting sounds complicated the first time it is described, but it is genuinely simple in practice: your host runs FreeBSD, inside the host you start a `bhyve` virtual machine that also runs FreeBSD, and inside either the host or the guest you create jails. Each layer is independent, each layer has its own `dmesg`, and each layer is cheap to recreate from scratch if something goes wrong.

Make a snapshot of the host before you start. The chapter will ask you to change `/etc/devfs.rules`, to load and unload kernel modules, and to create small VMs. None of these are risky if handled carefully, but accidents happen, and the snapshot turns any mistake into a two-minute rollback. If your host is itself a VM in VirtualBox or VMware, the platform's own snapshot tool is the fastest way to do this. If your host is bare metal, a ZFS boot environment with `bectl(8)` is a good substitute.

One more note. This chapter's labs need a few packages that you may not already have installed. You will want `bhyve-firmware` for UEFI guest support, `vm-bhyve` for a more convenient front end to `bhyve` and its accessories, and `jq` for parsing JSON output during some of the more elaborate tests. The commands to install them are in the labs; do not install them now, but be aware that they are coming.

### Prerequisites

You should be comfortable with everything from earlier chapters. In particular, this chapter assumes that you already know how to write a loadable kernel module from scratch, how `probe()` and `attach()` fit together in the driver lifecycle, how softc is allocated and used, and how `device_t` relates to `devclass`. It assumes fluency with the `bus_read_*` and `bus_write_*` accessors from Chapter 15, basic familiarity with interrupt handlers from Chapter 18, and the portable-driver habits from Chapter 29. If any of that feels uncertain, a brief revisit to the earlier material will save you time here.

You should also be comfortable with ordinary FreeBSD system administration: reading `dmesg`, editing `/etc/rc.conf`, using `sysctl(8)`, and creating and destroying jails with `jail(8)` or its wrapper `service jail`. You do not need prior bhyve experience; the labs will walk you through it. You do not need prior container experience either, since FreeBSD's container story is ultimately jails dressed in different operational clothing.

### What This Chapter Does Not Cover

A responsible chapter tells you what it leaves out. This chapter does not teach the internals of the `bhyve(8)` hypervisor. It does not teach `libvirt(3)`, `qemu(1)`, or the Linux KVM subsystem. It does not turn you into an OCI container expert. It does not cover the `xen(4)` paravirtualisation code, since Xen has become a narrower niche in the FreeBSD ecosystem and `bhyve` is the FreeBSD-native story worth learning first. It mentions `jail(8)` at exactly the level a driver author needs, not at the depth a jail administrator would want. For the missing topics, the FreeBSD Handbook and the respective manual pages are your friends, and I will point to them when they are relevant.

Several topics that could plausibly appear in a virtualisation chapter have homes elsewhere in this book. Secure coding against hostile input is Chapter 31's subject, not this chapter's; you will meet privilege boundaries and jail visibility here, but the deeper security discipline (Capsicum, MAC framework, sanitiser-driven fuzzing) waits for the next chapter. Advanced DMA under virtualisation (IOMMU setup, mapping pages with `bus_dmamap`) is touched conceptually here and developed more fully in later chapters. Performance tuning under virtualisation (how to measure paravirtual overhead, how to decide between emulated and passthrough for a given workload) is a theme that returns in Chapter 35. We will mention these where they connect, but we will not go deep.

### Structure and Pacing

Section 1 establishes the mental model: what virtualisation and containerization mean for a driver author, and how they differ from "hardware, but slower." Section 2 explains the three styles of guest device (emulated, paravirtualised, passthrough) and what each one implies for the driver. Section 3 takes a careful, beginner-friendly tour of VirtIO: the shared-ring model, feature negotiation, and the `virtqueue(9)` APIs you will use. Section 4 teaches how a driver detects its runtime environment through `vm_guest` and friends, and when that detection is and is not a good idea. Section 5 turns to the host side and looks at `bhyve(8)`, `vmm(4)`, and PCI passthrough. Section 6 covers jails, `devfs`, and VNET: the FreeBSD containerisation story from the driver's perspective. Section 7 addresses resource limits and privilege boundaries. Section 8 talks about testing and refactoring. Section 9 revisits time, memory, and interrupt handling through the lens of virtualisation, the quieter topics where beginner drivers often fail in subtle ways. Section 10 widens the lens to FreeBSD on arm64 and riscv64, whose virtualisation stories have their own shape. The labs and challenges follow, along with a troubleshooting appendix and a closing bridge into Chapter 31.

Read the sections in order. Each one assumes the previous one, and the labs depend on the earlier sections having been read and internalised.

### Work Section by Section

A recurring pattern in this book is that each section does one thing. Do not try to read two sections at a time, and do not skip ahead to a "more interesting" section if something in the current one feels difficult. The interesting parts of this chapter rest on the foundational ones, and a reader who has skipped the foundation will spend more time reverse-engineering the labs than the careful reader spends on the whole chapter.

### Keep the Reference Driver Close

Several labs in this chapter build on a small pedagogical driver called `vtedu`. You will find it under `examples/part-07/ch30-virtualisation/`, organised the same way as earlier chapters' examples. Each lab directory contains the state of the driver at that step, along with Makefile, README, and supporting scripts. Clone the directory, type along, and load the module after each change. Refactoring a VirtIO guest driver in your head is harder than refactoring one on disk; the feedback from the build system and from `dmesg` is half the lesson.

### Open the FreeBSD Source Tree

Several sections will point to real FreeBSD files. The ones that repay careful reading in this chapter are `/usr/src/sys/dev/virtio/random/virtio_random.c` (the smallest complete VirtIO driver in the tree), `/usr/src/sys/dev/virtio/virtio.h` and `/usr/src/sys/dev/virtio/virtqueue.h` (the public API surfaces), `/usr/src/sys/dev/virtio/virtqueue.c` (the ring machinery), `/usr/src/sys/dev/virtio/pci/virtio_pci.c` and `/usr/src/sys/dev/virtio/mmio/virtio_mmio.c` (the two transport backends), `/usr/src/sys/sys/systm.h` (for `vm_guest`), `/usr/src/sys/kern/subr_param.c` (for the `vm.guest` sysctl), `/usr/src/sys/net/vnet.h` (for VNET primitives), and `/usr/src/sys/kern/kern_jail.c` (for prison and jail APIs). Open them when the text says to, and read around the exact spot the text points at. The files are not decoration; they are the source of truth.

### Keep a Lab Logbook

Continue the lab logbook from earlier chapters. For this chapter, log a short note for each major lab: which commands you ran, which modules loaded, what `dmesg` said, what surprised you. Virtualisation work is easy to forget the details of after a few days, and a paper trail turns every future debugging session into a one-minute lookup rather than a half-hour re-derivation.

### Pace Yourself

Several ideas in this chapter will feel new the first time you meet them: shared-ring memory, feature negotiation, VNET's thread-local current vnet, the devfs ruleset number system. That newness is normal. If you feel your understanding blur during a particular subsection, stop. Re-read the previous paragraph, try a small experiment in the shell, and come back fresh in an hour. Consistent thirty-minute sessions produce better understanding than a single exhausted all-day effort.

## How to Get the Most Out of This Chapter

Chapter 30 rewards curiosity, patience, and a willingness to experiment. The specific patterns it introduces, the ring structures, the feature bits, the prison and vnet scopes, are not abstract. Each one corresponds to code you can read, state you can observe with `sysctl` or `vmstat`, and behaviour you can trigger with short commands. The most valuable habit you can build while reading the chapter is to move freely between the text, the source tree, and the running system.

### Read With the Source Tree Open

Do not read the VirtIO section without `virtio.h`, `virtqueue.h`, and `virtio_random.c` open in another window. When the text says that a driver calls `virtqueue_enqueue` to hand a buffer to the host, scroll to that function and see it in context. When the text mentions the `VIRTIO_DRIVER_MODULE` macro, open `virtio.h` and see the two `DRIVER_MODULE` lines it expands into. The FreeBSD kernel is far more approachable than its reputation suggests, and the only way to confirm that is to keep opening it.

### Type the Labs

Every line of code in the labs is there to teach something. Typing it yourself slows you down enough to notice the structure. Copy-pasting the code often feels productive and usually is not; the finger-memory of typing kernel code is part of how you learn it. If you must copy, copy one function at a time, and read each line as you paste it.

### Run What You Read

When the text introduces a command, run it. When the text introduces a `sysctl` name, query it. When the text introduces a kernel module, load it. The running system will surprise you sometimes (the `vm.guest` value may not be what you expect if your lab itself is a VM), and every surprise is an opportunity to learn something the text did not need to explain explicitly. A running FreeBSD system is a patient tutor.

### Treat `dmesg` as Part of the Manuscript

A significant fraction of what this chapter teaches is visible only in the kernel's log output. A VirtIO device's negotiated feature set, the attach sequence of a guest driver, the interaction between a module and a jail, all of these surface in `dmesg`. Read it often. Tail it during the labs. Copy relevant lines into your logbook when they teach something non-obvious. Do not treat `dmesg` as noise; treat it as the ground truth of what the kernel actually did.

### Break Things Deliberately

At three points in the chapter I will suggest deliberately breaking something to see what happens. These are the most educational moments you can give yourself. Unload a required module before a driver that depends on it and see the dependency graph complain. Remove a devfs rule and see the device vanish from inside the jail. Boot a guest with one CPU and then with four and compare the virtqueue setup. Deliberate failures teach in a way that success cannot.

### Work in Pairs When You Can

If you have a study partner, this is a great chapter to pair on. One of you can run the host and watch `dmesg`, while the other runs the guest and drives the driver. The two perspectives teach different things, and each of you will notice what the other missed. If you are working alone, use two terminal tabs and alternate attention between them.

### Trust the Iteration, Not the Memorisation

You will not remember every flag, every enum, every macro in this chapter on the first read. That is fine. What matters is that you remember where to look, what the overall shape of the subject is, and how to tell when your driver is doing the right thing. The specific identifiers will become second nature after you have written and debugged two or three virtualisation-aware drivers of your own; they are not a memorisation exercise.

### Take Breaks

Virtualisation-aware debugging has a particular cognitive cost. You are tracking state on several sides at once: the host, the guest, the driver, the device model, the jail. Your mind gets fatigued faster than when you are working on a single-threaded bare-metal driver. Two hours of focused work followed by a real break is almost always more productive than four hours of grinding.

With those habits in place, let us begin.

## Section 1: What Virtualisation and Containerization Mean for Driver Authors

Before we touch any code, we need to agree on what we are talking about. The words "virtualisation" and "containerization" have been worn smooth by marketing language, and they carry different meanings depending on where you encounter them. A vendor's white paper uses "virtualisation" as a synonym for running multiple operating systems on the same hardware; a cloud provider's documentation uses it as shorthand for any managed workload; a FreeBSD administrator uses it for anything from `bhyve(8)` to `jail(8)`. For a driver author, those meanings are not interchangeable. The shape of the problem you are solving changes depending on which kind of virtualisation your driver meets. This section anchors the vocabulary for the rest of the chapter.

### Two Families, Not One

The first distinction to draw is between **virtual machines** and **containers**. They solve different problems and produce different constraints for driver authors. Conflating them is the single most common confusion in this territory.

A **virtual machine** is a software-emulated computer. A hypervisor, which itself runs on real hardware, creates an execution environment that looks like a complete machine to the software inside it. The machine has a BIOS or UEFI, memory, CPUs, disks, and network cards. The operating system inside the VM boots from scratch just as it would on real hardware, loads a kernel, and attaches drivers. The observation that matters is that the **guest kernel is a full kernel**, independent of the host kernel. A FreeBSD guest running under a FreeBSD host is not sharing kernel code with the host; the two kernels are peers, separated by the hypervisor and by the barrier between guest memory and host memory.

A **container**, on the FreeBSD sense of the word, is a different animal. A container is a namespaced partition of a single kernel. The host kernel is the only kernel in the picture; the containers are separate process groups, separate filesystem views, and often separate network stacks, but all of them run on the same kernel with the same drivers. On FreeBSD, the classical container is a `jail(8)`. Modern container runtimes like `ocijail` and `pot` add scripting and orchestration around that same jail primitive. The observation here is that **there is only one kernel**. A driver in the host is visible to every jail, subject to the rules the host imposes; a jail cannot load a driver of its own, and it does not have a kernel into which to load one.

These two families overlap only in superficial ways. A VM and a jail both look like "another system" from the outside. A VM and a jail both have their own `/`, their own network, and their own users. But the kernel boundary is nowhere near the same, and that matters enormously to drivers.

A VM runs a full guest kernel; the kernel sees virtualised (or passthrough) hardware; the driver you write for that hardware is a **guest driver** that attaches inside the guest. A jail shares the host's kernel; no guest driver is being written, because there is no guest kernel to load it into; instead, the question is how a **host driver** exposes its services to processes running inside the jail. The techniques you use in each case are different.

For the rest of this chapter, when the text says "virtualisation," it usually means the VM case unless the context makes clear that jails are being discussed. When the text says "containerization," it usually means the jail case. Read those words with the families in mind, and you will avoid most of the confusion that newcomers have.

### Virtual Machines as an Environment for Drivers

A VM presents hardware to the guest. Exactly what that hardware looks like depends on the hypervisor's configuration. There are three broad styles of device that a guest may meet, each of which changes the driver problem.

**Emulated devices** are the oldest approach. The hypervisor presents to the guest a device that imitates a real, usually well-known, physical device: a `ne2000` Ethernet card, for example, or a PIIX IDE controller, or a serial port compatible with the venerable 16550 UART. The hypervisor's device model implements the same register interface the real hardware would expose. The guest loads the driver it would have loaded on real hardware, and the driver runs unchanged. The price is performance: every register access in the guest traps into the hypervisor, which emulates the behaviour in software, which is much slower than real hardware. Emulated devices are perfect for compatibility and for booting unmodified guest kernels; they are poor for serious workloads.

**Paravirtual devices** solve the performance problem by replacing the compatibility interface with one designed to be cheap for both sides. Instead of imitating physical hardware, the device defines a new interface that is efficient when implemented in software. VirtIO is the canonical example and the one this chapter will spend most of its time on. A VirtIO device does not look like any real hardware; it looks like a VirtIO device, which is a standardised interface that exposes shared memory rings and a few control registers and very little else. The guest must have a driver for VirtIO specifically; that driver is smaller and faster than the equivalent emulated-device driver, because the interface was designed for software on both sides.

**Passthrough devices** go in the other direction. The hypervisor steps aside and hands the guest a real piece of hardware: a physical NIC, a physical GPU, a physical NVMe SSD. The guest's driver talks to that hardware more or less directly. The hypervisor still mediates (through an IOMMU, for example, to constrain which memory the device can reach) but it no longer emulates or paravirtualises. Passthrough is fast but brittle: the device is no longer shared with other guests, and moving the VM to a different host is no longer trivial.

Each style of device imposes a different design on your driver. An emulated-device driver is typically an old, well-tested driver for the emulated hardware, which just happens to be running on a virtualised version of the same hardware. A paravirtual driver is usually a driver written specifically for the paravirtual interface, with no expectation that the "hardware" exists in real silicon. A passthrough driver is the same driver that would have run on bare metal, with one subtlety: the guest's memory is not the same memory the device DMAs into, and the IOMMU mapping must be correct for the device to work.

The rest of this chapter leans strongly into the second style, because paravirtualisation is where most of the novel driver work in virtualised environments happens, and because it illustrates a programming model (shared rings, feature bits, descriptor-based transactions) that generalises to other parts of FreeBSD.

### Containers as an Environment for Drivers

A jail is a different kind of environment, and it asks a different question of a driver. The driver is not running inside the jail; it is running in the single shared kernel. What the jail does is change what the processes in the jail see.

From the host's kernel, nothing changes when a jail is created. The host kernel still has all the drivers it had before; the drivers still attach to the same devices; the same `/dev/` entries still exist in the devfs mounted at the host's root. From inside the jail, a devfs mount is also performed, but that devfs is configured with a ruleset that hides some devices and exposes others. A `/dev/mem` is not normally visible inside a jail, because a process inside a jail should not be able to peek at kernel memory; a `/dev/kvm` equivalent (if FreeBSD had one) would be similarly hidden. A `/dev/null` and `/dev/zero` are visible, because there is no reason to hide them.

So what is a "driver problem" in a container? Two things, mostly. First, when your driver creates device nodes, you must decide whether those nodes should be visible to jailed processes, and if so, under what conditions. If your driver exposes an `ioctl` that allows a process to change the routing table, that ioctl should not be accessible to a process inside a jail that does not have the privilege to reconfigure the host's network. Second, when your driver cooperates with VNET (the FreeBSD virtual network stack framework), you must be careful about which global state is per-vnet and which is shared. An `ifnet` moved into a VNET jail should behave correctly inside that jail, which means your driver must have declared the right VNET-scoped variables and must not have cached any cross-vnet state.

These are different concerns from those of VM drivers, and they deserve their own sections. Sections 6 and 7 of this chapter work through them in detail.

### Why This Matters for Driver Authors

It is fair to ask: why should a driver author care about any of this? Until recently, most drivers were written for physical hardware on bare metal, and the "virtualisation story" was something that happened upstream. Today, and for the foreseeable future, the majority of FreeBSD installations are virtual machines. A driver that works only on bare metal is a driver that works in a minority of deployments. A driver that works only on the host, and not inside a jail, is a driver that cannot be used in any modern containerised FreeBSD environment. Virtualisation-aware driver design is not optional anymore; it is part of writing drivers at all.

There are three concrete reasons the topic matters.

First, **most of your users are inside a guest**. The FreeBSD VMs running in public clouds, in private clouds, and in local `bhyve` labs outnumber the FreeBSD installations on real hardware, perhaps by a wide margin. A driver whose design fails in a virtual environment will fail for most of its users.

Second, **host-side drivers expose services to jails**. Even if your driver is about a physical device that lives in the host, the moment a jail wants to use its services, the driver faces the jail question. It may need to expose a device node through the jail's devfs, or decide that certain ioctls are jailed-off.

Third, **performance at scale matters more than ever**. In a virtualised environment, the difference between an emulated driver and a paravirtual one is often a factor of ten or more in throughput. Knowing how to write and recognise paravirtual patterns is worth real engineering time. A few extra paragraphs of understanding about `virtqueue(9)` can save you a day of debugging later.

### Virtualisation Is Not Just Hardware But Slower

A common misconception is that virtualisation simply makes hardware slower. That view misses the mechanism entirely. In a well-designed virtualised environment, the guest driver does not see "slower hardware"; it sees different hardware with different access patterns. A VirtIO driver does not make one slow register read per descriptor and then complain about the overhead; it batches a dozen descriptors into a shared ring, notifies the host once, and waits for a single completion. The difference between a naive port of a physical-hardware driver to VirtIO and an idiomatic VirtIO driver is not a matter of tuning; it is a matter of architectural intent.

Similarly, containerization is not "processes, but confined." It is a reorganisation of visibility, privilege, and global state. A driver that exports a sysctl tree has to decide whether that tree should be visible inside a jail, and whether writes from inside the jail should affect only the jail's view or the host's. These are not just configuration questions; they are design questions that shape the code.

If you take one thing from this section, let it be this: **the environment your driver runs in is not a transparent wrapper around a machine**. It imposes its own concerns, offers its own APIs, and rewards or punishes driver designs that respect or ignore them.

### A Quick Taxonomy

To fix the vocabulary in your mind, here is a compact taxonomy of the environments we will discuss in this chapter. Treat it as a map you can return to when a later section mentions a term.

| Environment | Kernel Boundary | What the Driver Sees | Typical FreeBSD Example |
|------------|-----------------|----------------------|-------------------------|
| Bare metal | Full | Real hardware | Any driver |
| Emulated VM device | Full, at guest kernel | Imitation of real hardware | `xn`, `em` emulated by QEMU |
| Paravirtual VM device | Full, at guest kernel | Shared-ring interface | `virtio_blk`, `if_vtnet` |
| Passthrough VM device | Full, at guest kernel | Real hardware with IOMMU constraints | `em` on a passthrough NIC |
| Jail (no VNET) | Shared with host | Host-side driver; jail sees host's devfs filtered by ruleset | `devfs` visibility for `/dev/null` |
| VNET jail | Shared with host | Host-side driver; jail owns its network stack; interfaces moved via `if_vmove()` | `vnet.jail=1` in `jail.conf` |

Notice the kernel-boundary column. The boundary is the single most important feature of each row. VMs have a boundary; jails do not. Everything else follows from that.

### Wrapping Up

We have drawn the first map of the chapter. Virtual machines and containers are two different families. VMs present hardware (emulated, paravirtual, or passthrough) to a full guest kernel. Containers partition a single host kernel into isolated process environments, changing what they see and what they may do, but not introducing a second kernel. A driver author deals with each family differently: writing guest drivers for VMs, adapting host drivers for jails. The next section takes the first of those two worlds and looks at the three device styles a guest may meet, in enough detail to build intuition before we touch VirtIO specifically.

## Section 2: Guest Drivers, Emulated Devices, and Paravirtualised Devices

A guest kernel running inside a VM must attach drivers to the devices its hypervisor has given it. Those devices fall into the three styles introduced in Section 1: emulated, paravirtual, and passthrough. Each style produces a different kind of driver, a different kind of bug, and a different kind of optimisation opportunity. This section examines them in the order you are most likely to encounter them, and begins to build the vocabulary we will need in Section 3 for VirtIO specifically.

### Emulated Devices in Detail

An emulated device is the simplest story to tell. The hypervisor implements, in software, a faithful imitation of a known physical device. The classical FreeBSD examples include the emulated `em(4)` Ethernet NIC (the Intel 8254x family), the emulated `ahci(4)` SATA controller, the emulated 16550-compatible serial port, and the emulated VGA video adapter.

From the guest's point of view, nothing unusual is happening. The PCI device enumerator finds an Intel card; the `em(4)` driver probes it and attaches; the driver issues its usual register writes; packets eventually appear on the wire. Under the hood, each of those register writes is a trap into the hypervisor, which consults its model of the Intel chip, applies the state change, and possibly generates a virtual interrupt back into the guest.

This approach has one huge virtue and one large vice. The virtue is compatibility: the guest kernel needs no special driver at all. A stock FreeBSD install image boots on an emulated `em(4)` card as easily as it boots on a real one. The vice is cost. Every trap into the hypervisor, every software emulation of a register behaviour, every synthesised interrupt, costs CPU cycles. For a busy workload with millions of register accesses per second, emulated devices are measurably slow.

### What Emulated Devices Imply for Driver Code

From the driver author's perspective, an emulated device looks like real hardware. The implication is that you do not normally write a "special" driver for emulated hardware; you write one driver that works on the real silicon and lean on emulation for the compatibility case. Most of the emulated-device drivers in `/usr/src/sys/dev/` are therefore the same files that drive the real cards.

There are two small exceptions that matter for our purposes. First, some drivers include a short early-boot probe that distinguishes "this is a real card" from "this is a hypervisor imitating a card." The `em(4)` driver, for example, logs a slightly different message when it detects that the environment is virtualised, because some diagnostic counters are meaningful only on real silicon. Second, a few drivers bypass hardware-specific optimisations in virtualised environments because those optimisations would be counterproductive: prefetching blocks that the hypervisor's emulation already has in RAM is wasteful, for instance. These are rare and usually framed as conditional performance tweaks rather than architectural changes.

In practice, the first time you care about emulated-versus-real is when you are looking at a trace or a sysctl and wondering why a counter is zero. It is almost never a correctness problem.

### Why Emulation Exists at All

It is worth pausing to ask why emulated devices exist, given that they are slower than alternatives. The answer is threefold.

First, emulation provides compatibility with existing guest kernels. A hypervisor that offered only paravirtual devices would need to persuade every guest operating system to install paravirtual drivers. That is a solved problem today, but in the early 2000s (when modern hypervisors were being designed) it was a significant barrier. Emulated devices let guests run unmodified; they gained a performance path (paravirtual) later.

Second, emulation is often good enough for low-frequency workloads. A guest that makes one disk I/O per second does not suffer measurably from emulation. A guest that makes a hundred thousand disk I/Os per second suffers badly. The majority of workloads are closer to the first than the second, and for them, emulation is a fine choice.

Third, emulation is easier to implement correctly than paravirtualisation when the guest side is an existing driver. A hypervisor author who wants to support, say, VMware-format disk images does not have to write a paravirtual disk driver for every guest OS; they write a VMware-compatible emulated disk controller, and the existing VMware guest drivers work.

In FreeBSD's `bhyve(8)`, emulated devices include the LPC bridge (for serial ports and the RTC), PCI-IDE and AHCI controllers (for some storage cases), and the E1000 NIC (through the `e1000` backend). These are used for installers and for compatibility with older guests. For performance-oriented work, VirtIO devices are the common choice.

### Passthrough as Seen by the Guest

Passthrough deserves a deeper look from the guest's perspective, because it is the most "hardware-like" of the three styles and introduces subtleties that a novice driver author might not expect.

When a PCI device is passed through, the guest sees an exact copy of the device's PCI configuration: vendor ID, device ID, subsystem IDs, BARs, capabilities, MSI-X table. The guest's PCI enumerator claims the device with the same driver it would have used on bare metal. The driver programs the device through register accesses; those accesses are, for the most part, direct (not trapped and emulated), because the hardware virtualisation extensions (Intel VT-x, AMD-V) support mapping a device's MMIO into a guest's address space.

DMA is where subtlety enters. The guest programs physical addresses into the device's DMA registers, but those addresses are guest-physical, not host-physical. Without help, the device would DMA to host-physical addresses that do not correspond to guest memory at all, which would be a security disaster. The help comes from the IOMMU: Intel VT-d or AMD-Vi sits between the device and the host memory bus, and it remaps device-issued addresses to the correct host memory.

From the guest driver's perspective, all of this is invisible, as long as the driver uses `bus_dma(9)` correctly. The `bus_dma(9)` framework records which physical addresses are valid for a given DMA handle, and the kernel sets up the IOMMU mappings to match. A driver that bypasses `bus_dma(9)` and programs physical addresses directly (perhaps by calling `vtophys` on a kernel pointer) is a driver that will work on bare metal but break under passthrough.

Interrupts under passthrough are handled by MSI or MSI-X exclusively; legacy pin-based interrupts cannot be passed through usefully. The guest driver configures MSI/MSI-X in the usual way, and the hypervisor sets up interrupt remapping so that the device's interrupts are delivered to the guest's virtual interrupt controller rather than to the host's.

### Firmware and Platform Dependencies Under Passthrough

One area where passthrough can surprise driver authors is firmware. Many modern devices expect their firmware to be loaded by the host, not by the device itself. A device whose driver assumes firmware has already been loaded (perhaps by a boot-time BIOS routine on the host) may fail to initialise inside a guest, because the guest's BIOS has not done the firmware-loading work.

The solution is usually for the driver to carry firmware and load it explicitly. FreeBSD's `firmware(9)` framework makes this straightforward: the driver registers the firmware image (typically a blob compiled into a loadable module), and at attach time it calls `firmware_get` and uploads the blob to the device. A driver that works on both bare metal and passthrough is a driver that does the firmware load itself, rather than relying on platform code.

Similarly, ACPI tables and other platform tables may be different between the host and a guest. A driver that reads an ACPI table to determine board-specific routing (for example, to identify which GPIO controls a power rail) will find a different table in a guest, because the guest's virtual BIOS generates its own. The driver must either provide defaults for cases where the table is missing or treat the missing table as an unsupported platform.

### When to Prefer Which Device Style

The three device styles are not mutually exclusive on a single VM. A `bhyve(8)` guest can have an emulated LPC bridge, a VirtIO block device, a passthrough NIC, and a VirtIO console, all at the same time. The administrator chooses which style to use for each role based on the trade-offs.

For installation and legacy guests, emulated devices are right. They require nothing from the guest and they work out of the box.

For typical FreeBSD guests doing ordinary work, VirtIO devices are right. They are fast, standardised, and well supported in FreeBSD's tree. Most production `bhyve(8)` deployments use VirtIO for disk, network, and console.

For performance-critical or hardware-specific workloads, passthrough is right. A guest that runs a GPU-accelerated workload needs a passthrough GPU; a guest that needs real network-card offload features needs a passthrough NIC.

As a driver author, the most likely device style you will write for is VirtIO, because that is where the new driver work is. Emulated and passthrough drivers are usually existing drivers; VirtIO drivers are designed from scratch for the paravirtual interface.

### Paravirtual Devices in Detail

Paravirtual devices are a deeper change. The hypervisor declines to imitate any physical device and instead defines an interface optimised for software on both sides. The guest must have a driver written for that interface; a stock legacy driver will not work. The benefit is performance: a paravirtual interface can batch, amortise, and simplify the host-guest interaction in ways that a realistic hardware model cannot.

The dominant paravirtual family on FreeBSD is VirtIO. VirtIO is not a FreeBSD invention; it is a cross-platform standard designed for use across Linux, FreeBSD, and other guest operating systems. FreeBSD's VirtIO implementation lives under `/usr/src/sys/dev/virtio/`, with individual drivers for block devices (`virtio_blk`), network devices (`if_vtnet`), serial consoles (`virtio_console`), SCSI (`virtio_scsi`), entropy (`virtio_random`), and memory ballooning (`virtio_balloon`). Other VirtIO device types exist (9p filesystems, input devices, sockets), and they too have FreeBSD drivers in varying states of maturity.

From the outside, a VirtIO device looks like a PCI device (when attached via PCI) or a memory-mapped device (when attached via MMIO, which is common on embedded and ARM systems). The guest kernel enumerates it the same way it would any other device: a PCI bus scan finds a device with vendor ID 0x1af4 and a device ID indicating the type, or the guest's device tree advertises a VirtIO MMIO transport with a known compatible string.

Once probed and attached, the driver talks to the device through a small set of primitives: device feature bits, device status, device configuration space, and a set of **virtqueues**. The virtqueues are the heart of the VirtIO model. Each one is a ring of descriptors in shared memory, writable by the guest and readable by the host; the host has a parallel ring of used descriptors that the guest reads to discover completions. All the actual I/O happens through the rings; the registers the driver touches are almost exclusively used for setup, notification, and feature negotiation.

This design is radically different from an emulated-device driver. A VirtIO driver almost never issues a register write during normal operation. It posts buffers into rings, notifies the host that new work is available, and reads completions from the ring when they arrive. The per-request overhead is a handful of memory accesses, not a register trap. When the system is busy, the driver can post many requests and notify once; the host can batch many completions and signal once. The result is an order-of-magnitude performance improvement over an equivalent emulated driver.

### Paravirtual Devices and the Driver Author

For a driver author, paravirtual devices are where a lot of new work happens in FreeBSD's driver ecosystem. VirtIO is well-defined and stable, and writing a new VirtIO-backed device type is a reasonable weekend project once you have read the foundational drivers. More importantly, the patterns of VirtIO (shared rings, feature negotiation, descriptor-based transactions) echo through other FreeBSD subsystems (notably `if_netmap` and portions of the network stack), so time invested in understanding them pays dividends.

This chapter will spend all of Section 3 on VirtIO fundamentals. For now, what matters is the framing: a paravirtual driver is a first-class guest driver that was designed to live inside a VM, with the performance characteristics that design affords.

### Passthrough Devices in Detail

Passthrough is the third style, and in some ways the most interesting because it collapses the virtualisation question back into the bare-metal question, with a twist.

In passthrough, the hypervisor declines to abstract the device at all. It hands the guest a real PCI or PCIe device: for example, a specific physical NIC in one of the host's slots, or a specific NVMe SSD, or a specific GPU. The guest's driver talks to that real hardware as if it were the host. The guest even receives the device's interrupts, through a mechanism the hypervisor provides.

There are three reasons passthrough exists. First, performance: a passthrough NIC delivers line-rate traffic to the guest with no software overhead per packet. Second, access to hardware features the hypervisor does not emulate: GPU acceleration, NVMe-specific queuing, hardware crypto engines. Third, licensing or certification: some proprietary drivers only work with real hardware and cannot be used through any emulation layer.

The cost of passthrough is isolation. Once a device is passed through to a guest, the host no longer owns it in any useful sense; it cannot use the NIC itself, and other guests cannot share it. Live migration becomes difficult or impossible, because the target host may not have the same physical device in the same slot. The guest owns the hardware until the guest stops running.

### Passthrough and the IOMMU

Passthrough sounds simple in principle, but it requires one major piece of hardware support: an IOMMU. An IOMMU is to DMA what an MMU is to CPU memory access: a hardware-enforced translation table between the view of memory a device has (its "DMA address") and real physical memory. Without an IOMMU, a passthrough device could DMA into any physical memory on the host, including the host's kernel data, with obvious and terrifying consequences. With an IOMMU, the hypervisor can constrain the device to DMA only into the memory regions assigned to the guest, preserving the security boundary.

On amd64 systems, the IOMMU is Intel VT-d (on Intel CPUs) or AMD-Vi (on AMD CPUs). It is usually enabled through a kernel configuration option and some BIOS settings. FreeBSD's `bhyve(8)` uses the IOMMU through the `pci_passthru(4)` mechanism, which we will meet in Section 5.

For the driver author, the IOMMU is invisible most of the time. The `bus_dma(9)` API in the guest behaves the same way it does on bare metal; the guest driver's `bus_dmamem_alloc()` and `bus_dmamap_load()` calls produce DMA addresses that, from the guest's point of view, look the same as they would on bare metal. The IOMMU translation happens one layer below, between the device and the real memory, and the guest driver does not participate in it.

The one place where the IOMMU matters for the driver author is when the IOMMU is misconfigured or missing, and DMA starts to fail in mysterious ways. If you ever see "DMA timeout" messages from a passthrough driver, the IOMMU is often the first suspect. Chapter 32 will go deeper into this.

### The Driver Author's Mental Model

If you step back from these three styles, a clear pattern emerges. The guest kernel always loads a driver; the driver always sees a device; the device's behaviour is always defined by some interface. What changes between the styles is which interface and who implements it.

In emulated devices, the interface is the real hardware's register model, and the hypervisor implements it in software. The driver is the real-hardware driver.

In paravirtual devices, the interface is a bespoke software-optimised model, and the hypervisor implements it natively. The driver is paravirtual-specific.

In passthrough devices, the interface is the real hardware's register model, and the hardware itself implements it, with the IOMMU as a gatekeeper. The driver is the real-hardware driver, running in the guest, with DMA addresses translated transparently.

The driver author is therefore working with the same skills in all three cases. The shape of the work differs only in which interface is in play. Once you understand that, the rest of this chapter becomes a study of specific interfaces, specific FreeBSD APIs, and specific deployment contexts.

### A Note on Hybrid Models

Real systems often mix these styles. A single guest may have an emulated serial port for early boot logging, a paravirtual NIC for main network traffic, a paravirtual block device for the root filesystem, and a passthrough GPU for acceleration. Each of those devices is governed by its own driver, and the guest's driver toolkit needs to know the patterns for all three. This is not unusual; it is the common case for any non-trivial VM.

As a driver author, the implication is that you usually do not need to build a universal driver for all three styles. You build the driver for the style your device uses, and you coexist with drivers for the other styles. The per-device decisions about which style to use are made by the hypervisor administrator, not by the driver.

### Wrapping Up

Three styles of guest device, three kinds of driver work, one underlying mental model: a driver talks to an interface, and the interface is implemented by whichever layer the hypervisor chooses. Emulated is easy but slow; paravirtual is fast and requires custom drivers; passthrough is near-native and requires an IOMMU. The rest of this chapter concentrates on the paravirtual style, with VirtIO as the example, because that is where most of the interesting new driver work in FreeBSD happens. In Section 3 we open the VirtIO toolkit and start examining its parts.

## Section 3: VirtIO Fundamentals and `virtqueue(9)`

VirtIO is a standard. It defines a way for a guest to talk to a device that exists only in software, without reference to any physical hardware. The standard is maintained by the OASIS VirtIO Technical Committee and is implemented by a number of hypervisors, including `bhyve(8)`, QEMU, and the Linux KVM family. Because the standard is shared, a VirtIO guest driver written for a FreeBSD guest will talk to a VirtIO device presented by any conforming hypervisor.

This section introduces the VirtIO model at the level of detail a driver author needs. It is long, because VirtIO has enough moving parts to justify the space, and short compared to the VirtIO specification itself, which spans hundreds of pages. We will focus on the parts you need to write a FreeBSD guest driver.

### The Ingredients of a VirtIO Device

A VirtIO device exposes a small number of elements to its guest driver. Understanding each one is the foundation for everything that follows.

The first element is the **transport**. VirtIO devices can appear over several transports: VirtIO over PCI (the usual case on amd64 desktops, servers, and cloud), VirtIO over MMIO (the usual case on ARM and embedded systems), and VirtIO over channel I/O (used on IBM mainframes). From the guest's view, the transport dictates how the device is enumerated and how registers are read, but once the driver has hold of a `device_t`, the transport recedes into the background. FreeBSD's VirtIO framework provides a transport-agnostic API.

The second element is the **device type**. Every VirtIO device has a single-byte type identifier (`VIRTIO_ID_NETWORK` is 1, `VIRTIO_ID_BLOCK` is 2, `VIRTIO_ID_CONSOLE` is 3, `VIRTIO_ID_ENTROPY` is 4, `VIRTIO_ID_BALLOON` is 5, `VIRTIO_ID_SCSI` is 8, and so on, as listed in `/usr/src/sys/dev/virtio/virtio_ids.h`). The type tells the guest what the device does; the guest dispatches to the right driver based on the type.

The third element is the **device feature bits**. Each device type defines a set of optional features, each represented by a bit in a 64-bit mask. Some features are universal (the ability to use indirect descriptors, for example), and some are device-specific (VirtIO block devices may advertise write-caching, discard support, geometry information, and more). The guest driver reads the device's advertised features, selects which of them the driver knows how to use, and writes back a negotiated feature mask. The device then agrees to the negotiated set and rejects any attempt to use a feature outside it. This negotiation is the mechanism by which VirtIO devices and drivers evolve without breaking each other.

The fourth element is the **device status**. A small byte-level register reports where in the lifecycle the device sits: acknowledged by the driver, driver found for this device type, features negotiated, device set up and ready, and so on. Writing to the status register drives the device through its lifecycle; reading it tells the driver where it is.

The fifth element is the **device configuration space**. Each device type has its own small layout of bytes that carry device-specific configuration: a block device's capacity, a network device's MAC address, a console device's port count. The guest reads the configuration space to learn device-specific details and occasionally writes to it to request a configuration change.

The sixth element, and the most important for this chapter, is the **set of virtqueues**. Each VirtIO device has one or more virtqueues, and the virtqueues are where almost all the actual work happens. Think of each virtqueue as a bounded-size bidirectional conveyor belt between the guest and the host. The guest places requests on the belt; the host consumes them, acts on them, and places completions on a parallel return belt; the guest reads the completions to learn what happened. The conveyor belt is implemented as a ring of descriptors in shared memory.

### Virtqueues and the Shared-Ring Model

At the heart of VirtIO is the virtqueue, the single most important abstraction in the whole protocol. A virtqueue is a ring of descriptors, held in memory that both the guest and the host can read and write. Its size is a power of two, chosen at device initialisation; typical values are 128 or 256 entries, though larger values are permitted.

Each descriptor in the ring describes a single scatter-gather buffer in guest memory: its guest physical address, its length, and a couple of flags (is this buffer readable or writable from the device's point of view, does this descriptor chain to a next descriptor, does it point to an indirect descriptor table). The driver fills descriptors, chains them as needed to represent multi-buffer transactions, and writes to a small index to announce that new descriptors are available. The host's device implementation walks the descriptors, performs the work they describe, and writes to its own index to announce that the work is done.

There are three rings inside a virtqueue, not one. The **descriptor table** holds the actual descriptors. The **available ring** (driven by the guest) lists the indices of descriptor chains the driver has made available. The **used ring** (driven by the host) lists the indices of descriptor chains the device has consumed and whose results are ready. The split is subtle but important: the driver writes to the available ring and reads from the used ring, while the host does the opposite. Each side reads what the other side wrote, without locks, through careful use of memory barriers and atomic index updates.

From the driver author's point of view, you almost never manipulate the rings by hand. FreeBSD's `virtqueue(9)` API abstracts them behind a handful of functions. You call `virtqueue_enqueue()` to push a scatter-gather list onto the available ring. You call `virtqueue_notify()` to let the host know new work is available. You call `virtqueue_dequeue()` to pull the next completion off the used ring. If you are polling rather than taking interrupts, you call `virtqueue_poll()` to wait for the next completion. The API hides the index arithmetic, the memory barriers, and the flag juggling; your code deals with scatter-gather lists and cookies.

The cookie mechanism deserves a line. When you enqueue a scatter-gather list, you supply an opaque pointer, the cookie, which the API remembers. When you dequeue a completion, you receive that cookie back. This lets the driver associate each completion with the request that produced it, without the ring having to carry any driver-specific context. A driver typically passes the pointer to a softc-level request structure as the cookie; dequeue gives back the same pointer, and the driver resumes processing.

### Feature Negotiation in Practice

Before the driver uses any of these mechanisms, it must negotiate features. The sequence is simple and looks the same in every VirtIO driver.

First, the driver reads the device's advertised feature bits with `virtio_negotiate_features()`. The argument is a mask of features the driver is prepared to use; the return value is the intersection of the driver's mask and the device's advertised bits. The device has now committed to supporting only that subset, and the driver knows exactly which features are in play.

Second, the driver calls `virtio_finalize_features()` to seal the negotiation. After this call, the device will reject any attempt to enable features outside the negotiated set. The driver's subsequent setup code can inspect the negotiated mask through `virtio_with_feature()`, which returns true if a given feature bit is present.

Third, the driver allocates virtqueues. The exact number and per-queue parameters depend on the device type. A `virtio_random` device allocates one virtqueue; a `virtio_net` device allocates at least two (one for receive, one for transmit), more if multiple queues are negotiated. Allocation uses `virtio_alloc_virtqueues()`, passing an array of `struct vq_alloc_info`, each entry describing one queue's name, callback, and maximum indirect-descriptor size.

Fourth, the driver sets up interrupts with `virtio_setup_intr()`. The interrupt handlers are the callbacks from step three. When the host has posted a completion to a queue, the guest receives a virtual interrupt, the handler runs, and it processes the completions by calling `virtqueue_dequeue()` in a loop.

This four-step sequence is the backbone of every VirtIO driver in `/usr/src/sys/dev/virtio/`. Read `vtrnd_attach()` in `/usr/src/sys/dev/virtio/random/virtio_random.c` and you will see it clearly: feature negotiation, queue allocation, and interrupt setup, in exactly that order. Read `vtblk_attach()` in `/usr/src/sys/dev/virtio/block/virtio_blk.c` and you will see the same sequence, with more moving parts because the device is more complex, but the same skeleton.

### A Walkthrough of `virtio_random`

Because `virtio_random` is the smallest complete VirtIO driver in the FreeBSD tree, it is the best one to read first. The whole file is under four hundred lines, and every line is there for a reason. Let us trace through the important parts.

The softc is small:

```c
struct vtrnd_softc {
    device_t          vtrnd_dev;
    uint64_t          vtrnd_features;
    struct virtqueue *vtrnd_vq;
    eventhandler_tag  eh;
    bool              inactive;
    struct sglist    *vtrnd_sg;
    uint32_t         *vtrnd_value;
};
```

The fields are the device handle, the negotiated feature mask (the device has no feature bits so this will always be zero), a pointer to the single virtqueue, an event handler tag for shutdown hooks, a flag that is set to true during teardown, a scatter-gather list used for every enqueue, and a buffer in which the device will store each batch of entropy.

The device methods table is the standard newbus skeleton:

```c
static device_method_t vtrnd_methods[] = {
    DEVMETHOD(device_probe,    vtrnd_probe),
    DEVMETHOD(device_attach,   vtrnd_attach),
    DEVMETHOD(device_detach,   vtrnd_detach),
    DEVMETHOD(device_shutdown, vtrnd_shutdown),
    DEVMETHOD_END
};

static driver_t vtrnd_driver = {
    "vtrnd",
    vtrnd_methods,
    sizeof(struct vtrnd_softc)
};

VIRTIO_DRIVER_MODULE(virtio_random, vtrnd_driver, vtrnd_modevent, NULL);
MODULE_VERSION(virtio_random, 1);
MODULE_DEPEND(virtio_random, virtio, 1, 1, 1);
MODULE_DEPEND(virtio_random, random_device, 1, 1, 1);
```

`VIRTIO_DRIVER_MODULE` is a short macro that expands to two `DRIVER_MODULE` declarations, one for `virtio_pci` and one for `virtio_mmio`. This is how the same driver attaches on both transports without any transport-specific code of its own; the framework routes it to whichever transport finds a matching device.

The `probe` function is one line:

```c
static int
vtrnd_probe(device_t dev)
{
    return (VIRTIO_SIMPLE_PROBE(dev, virtio_random));
}
```

`VIRTIO_SIMPLE_PROBE` consults the PNP match table that was declared above through `VIRTIO_SIMPLE_PNPINFO`. The match table tells the kernel that this driver wants devices whose VirtIO type is `VIRTIO_ID_ENTROPY`.

The `attach` function is more substantial. It allocates the entropy buffer and the scatter-gather list, sets up features, allocates a single virtqueue, installs a shutdown event handler, registers itself with the FreeBSD `random(4)` framework, and posts its first buffer into the virtqueue:

```c
sc = device_get_softc(dev);
sc->vtrnd_dev = dev;
virtio_set_feature_desc(dev, vtrnd_feature_desc);

len = sizeof(*sc->vtrnd_value) * HARVESTSIZE;
sc->vtrnd_value = malloc_aligned(len, len, M_DEVBUF, M_WAITOK);
sc->vtrnd_sg = sglist_build(sc->vtrnd_value, len, M_WAITOK);

error = vtrnd_setup_features(sc);      /* feature negotiation */
error = vtrnd_alloc_virtqueue(sc);     /* allocate queue */

/* [atomic global-instance check] */

sc->eh = EVENTHANDLER_REGISTER(shutdown_post_sync,
    vtrnd_shutdown, dev, SHUTDOWN_PRI_LAST + 1);

sc->inactive = false;
random_source_register(&random_vtrnd);

vtrnd_enqueue(sc);
```

Feature negotiation is trivial because the driver advertises `VTRND_FEATURES = 0`:

```c
static int
vtrnd_negotiate_features(struct vtrnd_softc *sc)
{
    device_t dev = sc->vtrnd_dev;
    uint64_t features = VTRND_FEATURES;

    sc->vtrnd_features = virtio_negotiate_features(dev, features);
    return (virtio_finalize_features(dev));
}
```

The feature mask of the device is irrelevant; the driver wants no features and gets no features, and the finalisation succeeds trivially. A more complex driver would build a non-zero mask and check it afterwards.

The virtqueue allocation asks for a single queue:

```c
static int
vtrnd_alloc_virtqueue(struct vtrnd_softc *sc)
{
    device_t dev = sc->vtrnd_dev;
    struct vq_alloc_info vq_info;

    VQ_ALLOC_INFO_INIT(&vq_info, 0, NULL, sc, &sc->vtrnd_vq,
        "%s request", device_get_nameunit(dev));

    return (virtio_alloc_virtqueues(dev, 0, 1, &vq_info));
}
```

The first argument to `VQ_ALLOC_INFO_INIT` is the maximum indirect-descriptor-table size, which is zero for this driver because it does not use indirect descriptors. The second argument is the interrupt handler callback, which is `NULL` because this driver uses polling rather than interrupt-driven completion. The third is the argument to that handler. The fourth is the output pointer for the virtqueue handle. The fifth is a format string that produces the queue's name.

The enqueue function is short:

```c
static void
vtrnd_enqueue(struct vtrnd_softc *sc)
{
    struct virtqueue *vq = sc->vtrnd_vq;

    KASSERT(virtqueue_empty(vq), ("%s: non-empty queue", __func__));

    error = virtqueue_enqueue(vq, sc, sc->vtrnd_sg, 0, 1);
    KASSERT(error == 0, ("%s: virtqueue_enqueue returned error: %d",
        __func__, error));

    virtqueue_notify(vq);
}
```

This pushes a scatter-gather list describing the entropy buffer onto the ring. The cookie is `sc` itself. The next two arguments are "readable segments" (none, the device writes the buffer) and "writable segments" (one, the entropy buffer). `virtqueue_notify()` kicks the host so it starts filling the buffer.

Because there are no interrupts, the driver uses polling to retrieve completions:

```c
static int
vtrnd_harvest(struct vtrnd_softc *sc, void *buf, size_t *sz)
{
    struct virtqueue *vq = sc->vtrnd_vq;
    void *cookie;
    uint32_t rdlen;

    if (sc->inactive)
        return (EDEADLK);

    cookie = virtqueue_dequeue(vq, &rdlen);
    if (cookie == NULL)
        return (EAGAIN);

    *sz = MIN(rdlen, *sz);
    memcpy(buf, sc->vtrnd_value, *sz);

    vtrnd_enqueue(sc);   /* re-post the buffer */
    return (0);
}
```

The lifecycle here is the full VirtIO cycle. Enqueue posts a buffer; the host fills it with entropy and marks the descriptor "used"; the driver dequeues the used descriptor, extracts the cookie and the length, copies the result out to its caller, and re-posts the buffer for the next round. If the queue is empty (no completion yet), `virtqueue_dequeue()` returns `NULL` and the driver returns `EAGAIN`.

This is the full shape of a VirtIO driver. Everything else, from `virtio_blk` to `if_vtnet`, is elaboration on this base: more queues, more features, more work per completion, more bookkeeping per request. The skeleton is the same.

### What the `virtqueue(9)` API Buys You

Step back for a moment and consider how much complexity the API abstracts away. The ring arithmetic, with its wraparound and its careful handling of available versus used indices, is entirely hidden. The memory barriers that keep the guest and the host in sync, which are subtle enough to be a common source of bugs in handwritten ring code, are placed by the API. The optional features (indirect descriptors, event indexes) are turned on and off based on negotiation without the driver having to know how they work.

The cost of that abstraction is that a driver author who only ever uses the API will not understand the rings themselves in depth. That is usually fine, but on the rare occasion when you debug a ring-layer mismatch (because you posted a descriptor with the wrong writable count, say, and the host is rejecting it) you will want to know what is going on. The VirtIO specification is the reference for the underlying ring layout, and `/usr/src/sys/dev/virtio/virtio_ring.h` is the FreeBSD side of the same layout. They repay reading at least once.

### Indirect Descriptors

A small optimisation worth knowing about is the indirect-descriptor feature (`VIRTIO_RING_F_INDIRECT_DESC`). When negotiated, it allows a driver to describe a multi-buffer transaction in an indirect descriptor table rather than a chain of descriptors on the main ring. The ring entry becomes a single descriptor that points to a block of descriptors, so a large scatter-gather list consumes only one slot on the ring rather than many.

Indirect descriptors matter in workloads that do large, scatter-gather-heavy transactions, such as network drivers sending packets with many fragments. For small drivers, they are an optional flourish. The `virtqueue(9)` API handles the mechanism transparently: if you pass a non-zero `vqai_maxindirsz` during queue allocation, the kernel can use indirects; if you pass zero, it cannot.

### A Deeper Look at the Ring Layout

For a reader who wants to know what `virtqueue(9)` is hiding, here is a brief tour of the underlying layout. The details matter only when you are debugging a ring-layer issue; you can skip this subsection on first reading.

Each virtqueue has three main structures in memory, allocated contiguously (with alignment requirements that differ slightly between legacy and modern VirtIO).

The **descriptor table** is an array of `struct vring_desc`, one entry per descriptor slot:

```c
struct vring_desc {
    uint64_t addr;     /* guest physical address of the buffer */
    uint32_t len;      /* length of the buffer */
    uint16_t flags;    /* VRING_DESC_F_NEXT, _WRITE, _INDIRECT */
    uint16_t next;     /* index of the next descriptor if chained */
};
```

The descriptor table has as many entries as the queue size (typically 128 or 256). Unused descriptors are linked into a free list maintained by the driver; used descriptors form chains that describe single transactions.

The **available ring** is what the driver writes and the device reads:

```c
struct vring_avail {
    uint16_t flags;
    uint16_t idx;                    /* producer index, monotonic */
    uint16_t ring[QUEUE_SIZE];       /* head-of-chain indices */
    uint16_t used_event;             /* for VRING_F_EVENT_IDX */
};
```

When the driver enqueues a new transaction, it picks a chain of descriptors from the free list, fills them in, places the head descriptor's index into `ring[idx % QUEUE_SIZE]`, and then increments `idx`. The increment uses a release-style memory barrier so that the device sees the new `ring` entry before it sees the new `idx`.

The **used ring** is what the device writes and the driver reads:

```c
struct vring_used_elem {
    uint32_t id;        /* index of the head descriptor */
    uint32_t len;       /* number of bytes written */
};

struct vring_used {
    uint16_t flags;
    uint16_t idx;                                  /* producer index */
    struct vring_used_elem ring[QUEUE_SIZE];
    uint16_t avail_event;                          /* for VRING_F_EVENT_IDX */
};
```

When the device finishes a transaction, it writes an entry to `ring[idx % QUEUE_SIZE]` with the head index and the byte count, then increments `idx`. The driver observes the increment and dequeues accordingly.

The subtlety of the format is the synchronisation. Guest and host are not synchronising through locks (they do not share locks); they are synchronising through ordered writes and atomic index updates. The spec is careful to specify the memory barriers on each side, and the `virtqueue(9)` implementation places them correctly. Getting the barriers wrong is a common bug in hand-rolled ring implementations; that is one reason to use the framework.

### Event Indexes: A Performance Detail

The `VIRTIO_F_RING_EVENT_IDX` feature allows both sides to suppress notifications when they are not needed. The mechanism is two extra fields, `used_event` in the available ring and `avail_event` in the used ring. Each side writes a value saying "interrupt me only when the other side's producer index passes this value".

The practical effect is that a driver producing requests at a high rate does not cause the host to be interrupted on every enqueue; instead, the host reads the current `used_event` and only delivers a notification when the producer index catches up. Similarly, a device completing transactions at a high rate does not cause the guest to be interrupted on every dequeue; the guest sets `avail_event` to suppress notifications it does not need.

This optimisation halves the notification overhead in worst-case workloads. It is a feature that most modern VirtIO drivers negotiate, and the `virtqueue(9)` API handles it transparently. As a driver author, you simply negotiate the feature and do not have to think about the details.

### Legacy Versus Modern VirtIO

VirtIO has two major versions. Legacy (sometimes called "virtio 0.9") was the original specification; modern ("virtio 1.0" and up) came later with a cleaner design. FreeBSD supports both.

The practical differences are in configuration-space layout, byte order, and a few feature-bit semantics. Legacy VirtIO puts configuration in native byte order; modern VirtIO is always little-endian. Legacy mandates some fields in a specific position; modern uses capability structures to describe the layout flexibly. Legacy defines a specific set of feature bits below bit 32; modern extends them above bit 32.

For driver authors, the framework hides most of these differences. Negotiating `VIRTIO_F_VERSION_1` puts the driver in modern mode; not negotiating it puts the driver in legacy mode. The `virtio_with_feature` helper checks the negotiated state. As long as your driver follows the `virtqueue(9)` API and uses `virtio_read_device_config` rather than direct configuration-space access, you can ignore the legacy/modern distinction almost entirely.

The one place where the distinction leaks is in how configuration-space fields are accessed. Legacy VirtIO uses `virtio_read_device_config` with a size argument and assumes native byte order; modern VirtIO assumes little-endian and uses helpers that byte-swap if the host is big-endian. The framework handles this, but a driver that does configuration-space access by hand (rather than through the framework) would have to be aware.

### Feature Bits Worth Knowing

Each VirtIO device type has its own set of feature bits. A few are worth knowing in the abstract, because they show up in many drivers.

- `VIRTIO_F_VERSION_1` indicates that the device supports the modern, version 1 of the VirtIO specification. Almost all modern drivers negotiate this bit.
- `VIRTIO_F_RING_EVENT_IDX` enables a more efficient form of interrupt suppression. When both sides support it, notifications and interrupts are only sent when they are actually useful, reducing overhead under load.
- `VIRTIO_F_RING_INDIRECT_DESC`, already mentioned, allows indirect descriptors.
- `VIRTIO_F_ANY_LAYOUT` relaxes the rules about how descriptors are ordered in a chain.

Each device type has its own set on top of these. For a block device, `VIRTIO_BLK_F_WCE` indicates that the device has a write cache; `VIRTIO_BLK_F_FLUSH` provides a flush command. For a network device, `VIRTIO_NET_F_CSUM` advertises offloaded checksum calculation. For entropy, there are no device-specific feature bits.

The general rule is: read the feature bits the device advertises, decide which the driver can use, ignore the others. Negotiation is not about "demanding" features; it is about agreeing on an intersection.

### A Second Example: Structure of virtio_net

It is worth a brief look at a larger VirtIO driver to see how the skeleton scales. The network driver `if_vtnet` lives in `/usr/src/sys/dev/virtio/network/if_vtnet.c`. It is roughly ten times the length of `virtio_random`, but the extra size comes from features and completeness, not from complexity in the core VirtIO interaction. Knowing how the scale-up works makes future reading more productive.

`if_vtnet` begins, like every VirtIO driver, with a module registration and a `device_method_t` table. The methods table is longer because `if_vtnet` hooks into the `ifnet(9)` framework; you will see `DEVMETHOD` entries for `device_probe`, `device_attach`, `device_detach`, `device_suspend`, `device_resume`, and `device_shutdown`, each implemented by the driver. The `VIRTIO_DRIVER_MODULE` macro at the bottom of the file registers the driver for both PCI and MMIO transports, exactly as `virtio_random` does.

The softc, `struct vtnet_softc`, is much larger than `vtrnd_softc`, but its role is the same: hold the state that the driver needs to serve the device. Notable additions include a pointer to the kernel's `ifnet` structure (`vtnet_ifp`), an array of receive queues (`vtnet_rxqs`), an array of transmit queues (`vtnet_txqs`), a feature mask, a cached MAC address, and statistics counters. Each queue structure contains its own virtqueue pointer, softc back-reference, taskqueue context, and a number of bookkeeping fields. A single multi-queue virtio-net device can have tens of virtqueues; each receive/transmit pair represents one queue pair that can run in parallel with the others.

The probe function uses the same `VIRTIO_SIMPLE_PROBE` pattern as `virtio_random`, but matching on `VIRTIO_ID_NETWORK` (which is 1). The attach function is substantial: it reads feature bits, negotiates them, allocates the queues, reads the device's MAC address from configuration space (`virtio_read_device_config`), initialises the `ifnet`, registers with the network stack, and sets up callouts for link-status polling. Each of these steps is a small function of its own, and the flow follows the same "negotiate, allocate, register, start" rhythm that `virtio_random` illustrates.

The receive path uses `virtqueue_dequeue` in a loop to drain completed packets. For each completed descriptor, the driver reads the packet header (`virtio_net_hdr`) that the device wrote to the first few bytes of the buffer, extracts metadata such as checksum-status flags and GSO segment sizes, and passes the packet up to `if_input`. If the packet is the last one in a batch, the driver re-posts fresh receive buffers to keep the queue primed for the next round.

The transmit path uses `virtqueue_enqueue` with a scatter-gather list covering both the packet header and the packet body. The cookie is the `mbuf(9)` pointer, so the transmit completion callback can free the mbuf when the device is done with it. If the queue becomes full, the driver stops accepting outbound packets until some space frees up, which is standard `ifnet` driver discipline.

Feature negotiation in `if_vtnet` is interesting because the feature set is large. Features like `VIRTIO_NET_F_CSUM` (transmit checksum offload), `VIRTIO_NET_F_GUEST_CSUM` (receive checksum offload), `VIRTIO_NET_F_GSO` (generic segmentation offload), and `VIRTIO_NET_F_MRG_RXBUF` (merged receive buffers) each change how the driver programs the virtqueues and how it interprets incoming data. The driver's feature-negotiation function picks a set that the driver implements, offers it for negotiation, and then queries the negotiated mask to decide which code paths to enable.

The lesson from `if_vtnet` is that a VirtIO driver scales through feature diversity and queue multiplicity, not through a fundamentally different architecture. If you understand `virtio_random`'s lifecycle, you understand `if_vtnet`'s lifecycle; the extra code is in feature-specific sub-paths and in `ifnet`-specific hooking. When the time comes to read `if_vtnet.c`, focus on `vtnet_attach` first, then walk the feature-negotiation function, then look at `vtnet_rxq_eof` (receive) and `vtnet_txq_encap` (transmit). Those four functions explain 80% of the driver.

### A Third Example for Intuition: virtio_blk

`virtio_blk`, in `/usr/src/sys/dev/virtio/block/virtio_blk.c`, is shorter than `if_vtnet` but longer than `virtio_random`. It illustrates a third common pattern: a driver that exposes a block-oriented device rather than a stream-oriented one.

The softc of `vtblk` contains a virtqueue, a pool of request structures, statistics, and the geometry information read from the device's configuration space. The probe and attach follow the familiar pattern. The interesting part is how requests are structured.

For every block I/O operation, the driver builds a three-segment descriptor chain: a header descriptor (containing the operation type and sector number), zero or more data descriptors (the actual payload, which is writable from the device's perspective for reads and readable from the device for writes), and a status descriptor (where the device writes a single-byte status code). The header and status are small structures; the data segments are whatever the `bio(9)` request brought in.

This three-segment layout is a common VirtIO idiom. If you see a driver building a chain that starts with a header and ends with a status, you are looking at a request-response device. `virtio_scsi` uses the same layout, as does `virtio_console` (for control messages). Recognising the pattern speeds up reading.

### Common Mistakes When Reading VirtIO Code

When reading any VirtIO driver for the first time, a few patterns can confuse a reader who is not expecting them.

The first is the mix of probe patterns. Some drivers use `VIRTIO_SIMPLE_PROBE`; others use their own probe functions that do more complex feature checks. Both are legitimate, and the former is a shorthand for a common case.

The second is the module registration style. `VIRTIO_DRIVER_MODULE` is a macro that expands to two `DRIVER_MODULE` calls, and reading the macro definition (in `/usr/src/sys/dev/virtio/virtio.h`) makes it clear. Without the macro, you might wonder why the driver is not calling `DRIVER_MODULE` explicitly; with it, you see that it is, once per transport.

The third is the distinction between the VirtIO core functions (`virtio_*`) and the virtqueue functions (`virtqueue_*`). The core functions operate on the whole device; the virtqueue functions operate on a single queue. A driver usually uses both, and the namespace prefix is the hint.

The fourth is the polling-versus-interrupt distinction. A driver that polls (like `virtio_random`) passes `NULL` as the callback in `VQ_ALLOC_INFO_INIT` and calls `virtqueue_poll` to wait for completions. A driver that uses interrupts (like `if_vtnet`) passes a callback and lets the kernel's interrupt infrastructure schedule it. Both are legitimate; the choice depends on the workload's latency sensitivity and on whether the driver can afford to block.

### Wrapping Up

This has been a long section, and deliberately so. VirtIO is dense, and its vocabulary is the foundation for most of the interesting work a modern guest-driver author does. You now know that a VirtIO device has a transport, a type, a feature mask, a status, a configuration space, and a set of virtqueues; that the virtqueues are shared rings with separate descriptor, available, and used components; that `virtqueue(9)` abstracts the ring mechanics behind a small API of enqueue, dequeue, notify, and poll; that feature negotiation is the mechanism for forward and backward compatibility; and that the smallest FreeBSD VirtIO driver is `virtio_random`, under four hundred lines of code that illustrate the whole skeleton. You have also seen how larger VirtIO drivers like `if_vtnet` and `virtio_blk` are structured around the same skeleton, scaled up with feature-specific code.

Section 4 takes a different turn. Now that you know what a guest driver is, we consider the question: how does the driver know it is in a guest in the first place?

## Section 4: Hypervisor Detection and Environment-Aware Driver Behaviour

There are legitimate reasons for a driver to know whether it is running inside a virtual machine. A driver that is inclined to use an expensive hardware counter may skip it on a hypervisor where the counter is known to be unreliable. A driver that polls a hardware register in a tight loop may tune its polling interval differently under virtualisation, because each poll becomes a hypervisor exit. A driver that emits a warning about a suspicious interrupt rate may suppress the warning when running in a cloud where background noise is normal. A few FreeBSD drivers in the tree use this kind of adaptation.

There are also illegitimate reasons. A driver that tries to behave differently to "hide" something from a hypervisor is building anti-debugging logic, which has its place in anti-tamper software but has none in a general-purpose driver. A driver that branches on the exact hypervisor brand to gain performance is building fragile code whose behaviour will drift as hypervisors evolve. We will focus on the legitimate uses.

### The `vm_guest` Global

The FreeBSD kernel exposes a single global variable, `vm_guest`, which records the hypervisor it detected at boot. The variable is declared in `/usr/src/sys/sys/systm.h`:

```c
extern int vm_guest;

enum VM_GUEST {
    VM_GUEST_NO = 0,
    VM_GUEST_VM,
    VM_GUEST_XEN,
    VM_GUEST_HV,
    VM_GUEST_VMWARE,
    VM_GUEST_KVM,
    VM_GUEST_BHYVE,
    VM_GUEST_VBOX,
    VM_GUEST_PARALLELS,
    VM_GUEST_NVMM,
    VM_LAST
};
```

The values are self-explanatory. `VM_GUEST_NO` means the kernel is running on bare metal; the other values identify specific hypervisors. `VM_GUEST_VM` is a fallback for "a virtual machine of unknown type," when the kernel could tell it was virtualised but could not pin down which hypervisor.

The corresponding sysctl is `kern.vm_guest`, which yields the human-readable form of the same information. The string values mirror the enum: "none", "generic", "xen", "hv", "vmware", "kvm", "bhyve", "vbox", "parallels", "nvmm". You can query it from the shell:

```sh
sysctl kern.vm_guest
```

On a bare-metal FreeBSD machine you will see `none`. On a `bhyve(8)` guest, you will see `bhyve`. On a VirtualBox guest, `vbox`. And so on.

### How the Detection Works

The detection happens early in boot, on architectures where hypervisor presence is detectable. On amd64, the kernel examines the CPUID leaf for a hypervisor present bit and then probes specific leaves associated with each hypervisor brand. The code lives in `/usr/src/sys/x86/x86/identcpu.c`, in functions named `identify_hypervisor()` and `identify_hypervisor_cpuid_base()`. On arm64, a similar mechanism exists through firmware interfaces.

The driver author does not need to care about the detection code. What matters is that by the time your driver's `attach()` runs, `vm_guest` has been set to its final value.

### When to Consult `vm_guest`

Consulting `vm_guest` is appropriate in a driver when the driver's correctness or performance depends on knowing the runtime environment. A few real examples from the tree illustrate the range:

- Some performance counters in `/usr/src/sys/kern/kern_resource.c` adjust their behaviour when `vm_guest == VM_GUEST_NO`, because the kernel assumes a bare-metal cost model is accurate there.
- Some time-related code in the amd64 tree may not use certain timing primitives under specific hypervisors where those primitives are known to be unreliable.
- Some informational messages in driver probe paths are suppressed under virtualisation to avoid confusing users who expect the driver to behave "differently" in a VM.

A driver you write should use `vm_guest` sparingly. A common anti-pattern is to test for `VM_GUEST_VM` as a blanket switch ("if virtualised, do X"), which is usually a sign that the driver has a bug on real hardware that it is papering over. Prefer to handle the underlying cause directly, and use `vm_guest` only when the dependency is genuinely on the environment rather than on a specific hardware quirk.

### A Usage Example

Suppose your driver has a sysctl that controls how aggressively it polls a hardware register. The tight loop is fine on bare metal, where each poll is cheap, but under virtualisation each poll costs a hypervisor exit, which is expensive. You might want the default polling interval to be larger under virtualisation:

```c
#include <sys/systm.h>

static int
my_poll_default(void)
{
    if (vm_guest != VM_GUEST_NO)
        return (100); /* milliseconds */
    else
        return (10);
}
```

This is a legitimate use of `vm_guest`: the driver is choosing a sensible default based on a property of the environment that genuinely affects its performance characteristics. The value remains overridable by a sysctl for users who know better than the default.

Contrast that with an illegitimate use:

```c
/* DO NOT DO THIS */
if (vm_guest == VM_GUEST_VMWARE) {
    /* skip interrupt coalescing because VMware does it weirdly */
    sc->coalesce = false;
}
```

Here the driver is branching on a specific hypervisor brand to work around a perceived bug. This is fragile for three reasons. The specific behaviour of VMware may change in a later release; the same issue may apply to KVM or Parallels without the code noticing; and the driver now has a maintenance burden tied to a third-party product. A better approach is to address the root cause (the coalescing code is fragile, so either fix it or expose a sysctl), not the symptom (VMware happens to trigger it).

### The Sysctl Path

For user-space tools and scripts, the `kern.vm_guest` sysctl is a better interface than poking at `/dev/mem` or similar. A small shell script can decide whether to run certain tests based on the environment:

```sh
if [ "$(sysctl -n kern.vm_guest)" = "none" ]; then
    echo "Running on bare metal, enabling hardware tests"
    run_hardware_tests
else
    echo "Running on a hypervisor, skipping hardware tests"
fi
```

This is how the FreeBSD test suite decides which tests to run in which environment. Reusing the same variable from inside a driver keeps the whole system consistent.

### Interaction with Subsystems

A few FreeBSD subsystems adapt their behaviour based on `vm_guest` automatically, and a driver author should be aware of the adaptations without having to touch them.

The timecounter selection code in `/usr/src/sys/kern/kern_tc.c` considers `vm_guest` when picking a default timecounter, because some hypervisors' TSC implementations are unreliable across guest migrations. The driver does not need to care; the kernel simply picks a safer timecounter.

The VirtIO transport drivers (`virtio_pci.c` and `virtio_mmio.c`) do not branch on `vm_guest` directly, because they are already guests by their own probe path. They trust that if they have a device at all, they are running in an environment that emulates VirtIO.

Some network driver code handles the case where GRO (generic receive offload) is known to interact badly with certain hypervisor configurations. These adaptations are not driver-by-driver decisions; they are made in the network stack based on observed characteristics.

As an author of a single driver, your rule of thumb is: if you feel the urge to branch on `vm_guest`, ask yourself whether the issue belongs to the kernel's environment handling rather than to your driver. Usually it does.

### Detecting Within a VirtIO Driver

A VirtIO driver, by definition, is running inside some kind of environment that speaks VirtIO. The driver does not usually need to know which specific hypervisor is backing the VirtIO device. That is one of VirtIO's virtues: it is environment-agnostic. The same `virtio_net` driver works under `bhyve`, under QEMU/KVM, under Google Cloud's backend, and under AWS Nitro, because all of them implement the same standard.

There are two exceptions. One is when the driver hits a performance cliff that is hypervisor-specific, in which case `vm_guest` tells you which workaround to apply. The other is when the driver wants to print a diagnostic message that identifies the hypervisor by name. Both are rare.

### Detecting From the Host Side

A host-side driver (one that runs in the host, not in a guest) usually does not care about `vm_guest` at all, because the host is not virtualised by definition. The sysctl will return `none` on bare metal, and that is the expected case.

The one subtlety is when the host is itself a guest, as in nested virtualisation scenarios. A FreeBSD host inside a VMware ESXi VM, for example, will have `vm_guest = VM_GUEST_VMWARE`. A `bhyve(8)` running inside such a host will present guests that also see virtualisation, though they see the `bhyve` presentation rather than the VMware one. The chain of environments can go two or three levels deep in research and test setups. Do not assume a host is bare metal; if your driver has a host-versus-guest split, use the sysctl or the variable to distinguish.

### Wrapping Up

`vm_guest` is a small, quiet API that tells your driver, and user-space tools, about the environment. It is easy to use and easy to misuse. Use it to adapt sensible defaults, not to work around hypervisor-specific bugs. Use it to inform user-space decisions through the sysctl. Do not make your driver's behaviour depend on the exact hypervisor brand; doing so couples your code to a third-party product's version history, and that is a maintenance tax you do not want to pay.

Section 5 looks at the other side of virtualisation for FreeBSD: the host that runs the guests, and the tools and interfaces that a driver author should understand when running or cooperating with `bhyve(8)`.

## Section 5: bhyve, PCI Passthrough, and Host-Side Considerations

So far we have focused on what happens inside the guest. The guest is where most of the learning happens, because the guest is where most of the driver code lives. But FreeBSD has another role in the virtualisation story: it is also a hypervisor. `bhyve(8)` runs virtual machines, and understanding how `bhyve(8)` presents devices to its guests is useful both when you are the guest author (so you know what the host is doing) and when you are the host author (so you know how to share a real device with a guest).

This section steps onto the host side. We will not go deep enough to write hypervisor code, because that is a topic of its own. We will go deep enough for you to understand what the host is doing, what the host-side knobs are, and what a driver author needs to know when cooperating with `bhyve(8)`.

### bhyve From the Driver Author's Perspective

`bhyve(8)` is a type-2 hypervisor that runs on a FreeBSD host and uses hardware virtualisation extensions (Intel VT-x or AMD-V) to execute guest code directly on the CPU. It is implemented as a user-space program (`/usr/sbin/bhyve`) and a kernel module (`vmm.ko`). The kernel module handles the low-level virtualisation primitives: VM entry and exit, page table management for guest memory, virtual APIC emulation, and a handful of performance-critical device backends. The user-space program handles the rest: command-line parsing, emulated device backends that do not need to live in the kernel, VirtIO backends, and the main VCPU loop.

From a driver author's perspective, three things matter about `bhyve(8)`.

First, `bhyve(8)` is a FreeBSD program, so the same kernel that runs your driver might also be running `bhyve(8)` in user space. That means host-side resources (memory, CPU, network interfaces) can be competing with `bhyve(8)` for allocation. If your driver is running on a host that is also a hypervisor, you may want to think about NUMA placement, IRQ affinity, and similar concerns.

Second, `bhyve(8)` uses a FreeBSD kernel interface called `vmm(4)` for its low-level needs. This interface is stable but niche; most driver authors never touch it directly. If you are writing a driver that needs to interact with virtual machines (for example, a driver that provides a paravirtual device to `bhyve(8)` guests from the host side), you would use `vmm(4)` or one of the higher-level libraries that wraps it.

Third, and most importantly for this chapter, `bhyve(8)` can assign a real PCI device directly to a guest. This is called PCI passthrough, and it has important implications for driver authors on both sides of the fence.

### vmm(4): The Kernel Side of bhyve

`vmm(4)` is a kernel module that exposes an interface for creating and managing virtual machines. It lives at `/usr/src/sys/amd64/vmm/` and related directories. The module is loaded on demand by `bhyvectl(8)` or `bhyve(8)`, and it exports a character device interface through `/dev/vmm/NAME` where `NAME` is the name of the virtual machine.

The `vmm(4)` interface is not something a beginner driver author needs to learn in depth. It is complex, specialised, and mostly of interest to people who are extending or modifying the hypervisor itself. For our purposes, it is enough to know the following. `vmm(4)` manages the virtual CPU state, including registers, page tables, and interrupt controllers. It hands off emulation of devices that are not performance-critical to user space, via a ring buffer interface. For devices that are performance-critical, such as the in-kernel virtual APIC or the virtual IOAPIC, it handles emulation in the kernel itself.

A driver author who is running inside a `bhyve(4)` guest will never interact with `vmm(4)` directly. The guest kernel sees only the virtualised devices; the mechanism by which they are emulated is invisible. The only observable trace is in `sysctl kern.vm_guest`, which will report `bhyve`.

A driver author who is writing host-side code for `bhyve(8)` will see `vmm(4)` only through the user-space libraries. Most tasks are handled by `libvmmapi(3)`, which wraps the raw ioctl interface. Direct `vmm(4)` work is rare outside of `bhyve(8)` development itself.

### PCI Passthrough: Giving a Guest a Real Device

The most direct way for a guest to interact with a physical device is for the host to give the guest exclusive access to that device. This is called PCI passthrough, and `bhyve(8)` supports it through the `pci_passthru(4)` facility.

The idea is straightforward, but the mechanics are subtle. A real PCI device is normally claimed by a driver on the host. That driver programs the device, handles its interrupts, and owns its memory-mapped registers. When we do passthrough, we want the guest's driver to do all of that instead. The host must step out of the way, and the hardware must be reconfigured so that the guest's memory addresses map to the device's memory correctly and so that the device's DMA operations go to the guest's memory rather than the host's.

The host steps out of the way by detaching the driver that was attached to the device and attaching a placeholder driver (`ppt(4)`) instead. `ppt` is a minimal driver whose only purpose is to claim the device so that no one else does. Its probe function matches any PCI device whose address matches a pattern specified by the user in `/boot/loader.conf`, typically using the `pptdevs` tunable. Once the placeholder driver claims the device, `bhyve(8)` can request a passthrough through the `vmm(4)` interface, and the device becomes accessible inside the guest.

The hardware reconfiguration is handled by the IOMMU. This is the part that makes passthrough both capable and dangerous. Without an IOMMU, DMA from the device would go to physical addresses on the host's memory bus, and a misbehaving guest could program the device to read or write anywhere in host memory. That is obviously unsafe. An IOMMU (Intel VT-d or AMD-Vi on the platforms FreeBSD supports) sits between the device and the host memory bus, remapping device-issued addresses so that they cannot escape the guest. From the device's perspective, it still does DMA to an address; from the host memory bus's perspective, that address is translated to somewhere inside the guest's memory and nowhere else.

If your host has no IOMMU, `bhyve(8)` will refuse to set up passthrough. This is an intentional safety check. Enabling passthrough without IOMMU protection would be like handing a stranger a signed blank cheque on the host kernel: one bug in the device firmware or one malicious guest, and the host is compromised.

### How Passthrough Looks to the Guest Driver

From the guest's perspective, a PCI passthrough device looks exactly like the real hardware. The guest's PCI enumeration finds the device, with its real vendor and device IDs, its real capabilities, and its real BARs. The guest's driver attaches just as it would on bare metal. The read and write operations hit the real hardware (with some address translation in between). Interrupts are delivered to the guest through the hypervisor's virtual interrupt controller. DMA works, though the addresses the guest programs into the device are guest-physical addresses, not host-physical addresses, with the IOMMU taking care of the translation.

This has three practical consequences for a driver author.

First, your driver does not need to know it is running under passthrough. The same driver binary works on bare metal and in a passthrough guest. This is a major design goal of the whole scheme.

Second, if your driver uses DMA, make sure you are using `bus_dma(9)` properly. The bus-DMA framework handles the guest-physical versus host-physical translation transparently if you use it correctly. If you are doing clever things with physical addresses directly (which you should not be), passthrough will break those things.

Third, if your driver relies on platform-specific features (for example, special firmware on the PCI device itself, or a particular BIOS table), those features must be present in the guest too. Passthrough gives the guest the device, but it does not give it the firmware or the BIOS tables. Some passthrough setups fail because the driver assumes the presence of an ACPI table that exists on the host but not inside the guest's virtual BIOS.

The last point matters especially for devices that expect strict ordering or uncached memory access. The bus-DMA and bus-space interfaces, used correctly, handle these cases. Direct pointer manipulation to mapped memory usually does not survive passthrough.

### Host-Side Driver Attach Under bhyve

When `bhyve(8)` is running on a FreeBSD host, two categories of device exist. Some devices are claimed by host drivers and shared with guests through emulation or VirtIO. Others are claimed by `ppt(4)` and passed through entirely.

If you are writing a driver for a device that might be used under passthrough, there are a few things to consider.

The first is whether the device should even allow passthrough. Some devices are fundamental to the host's operation (for example, the SATA controller that the host boots from, or the network interface the host is reachable on). These devices should not be marked as candidates for passthrough, because taking them away from the host would break the host. The mechanism for marking candidates is administrative, through `pptdevs` in `loader.conf`, and the host administrator is responsible. There is no per-driver lockout, but a driver author can document clearly whether passthrough is recommended for the device.

The second is whether the driver releases the device cleanly at detach time. Passthrough requires the original driver to detach, and then `ppt(4)` to attach. If your driver's `DEVICE_DETACH` method is sloppy, passthrough setup will be unstable. The detach code must stop the hardware cleanly, release IRQs, unmap resources, and free any memory that the hardware might touch. Anything that persists after detach is a risk.

The third is whether the driver tolerates being re-attached after a passthrough use. When the guest shuts down and releases the device, the host administrator might want to rebind the device to the original driver for use on the host. The driver should be able to attach to a device that was previously owned by `ppt(4)`, even though the device may have been reset and reconfigured by the guest. This means the driver's `DEVICE_ATTACH` method should not assume anything about the device's initial state; it should program the device from scratch, just as it does on first boot.

None of these are new requirements. They are all things a well-written driver does anyway. Passthrough just makes the habits more important, because the cost of getting them wrong shows up immediately.

### IOMMU Groups and the Reality of Partial Isolation

A brief word on IOMMU groups, which come up sometimes in passthrough discussions. An IOMMU cannot always isolate a single device from another device on the same PCI bus. When two devices share a bus or a bridge without ACS (Access Control Services), the IOMMU treats them as a single group, because it cannot guarantee that one cannot see the other's DMA. FreeBSD's `dmar(4)` (Intel) and `amdvi(4)` (AMD) drivers handle this grouping internally, but the administrator sometimes has to pass through a whole group rather than a single device.

For a driver author, the practical implication is that passthrough sometimes grabs more than the device you expected. If your device sits behind a bridge shared with another device, turning on passthrough for your device may pull the other one into the guest too. The solution is usually to place devices on separate bridges in the firmware configuration, but that is an administrator concern, not a driver concern. Knowing it exists helps when diagnosing unexpected behaviour.

### Host-Only Considerations: Memory, CPU, and the Hypervisor

A FreeBSD host that runs `bhyve(8)` guests has a few extra responsibilities beyond the usual. Memory for the guest's RAM is allocated from the host's physical memory, so a host running many guests needs correspondingly more memory. Guest VCPUs are backed by host threads, so a host running many guests needs to provision CPU capacity. And the hypervisor itself uses a small amount of memory and CPU for its bookkeeping.

A driver author on the host side does not need to manage these resources directly. They are managed by `bhyve(8)` and the host administrator. But there are two ways in which a host-side driver can interact with them.

The first is when a driver offers a device backend that `bhyve(8)` consumes. An example would be a storage driver that provides the backing store for a guest's virtual disk. If the host-side driver is slow, the guest feels it as slow disk. If the host-side driver consumes too much memory, the host runs out of memory for guests. This is a classic shared-resource problem, and it is usually solved by provisioning rather than by clever code.

The second is when a driver interacts with the hypervisor through `vmm(4)` or similar. Paravirtual device backends sometimes use kernel-side hooks to deliver notifications to guests more efficiently than going through user space. These are rare and advanced, and outside the scope of this chapter. They are mentioned here so you are not surprised if you see them referenced later.

### Cross-Platform Notes: bhyve on arm64

`bhyve(8)` runs on amd64 and, increasingly, on arm64. The arm64 port uses the ARMv8 virtualisation extensions (EL2) rather than Intel VT-x or AMD-V. The SMMU (System Memory Management Unit) takes the role of the IOMMU. The user-space interface is the same, the guest's VirtIO experience is the same, and from a driver author's perspective, the two architectures are interchangeable. The distinction matters only for the low-level hypervisor code inside `vmm(4)`.

For a book about driver authorship, the lesson is the one we have already learned in Chapter 29: write clean, bus-dma-using, endian-correct code, and you will not notice which architecture you are running on. The hypervisor story is another good reason to follow those habits.

### Inspecting bhyve From the Host

A driver author can inspect `bhyve(8)` state in a few useful ways without getting into `vmm(4)` internals.

`bhyvectl(8)` is the command-line tool for querying and controlling virtual machines. `bhyvectl --vm=NAME --get-stats` shows counters maintained by `vmm(4)` for a running guest, including VM exit counts, emulation counts, and similar diagnostics. This is useful when you suspect that a guest driver is generating unnecessary VM exits (a common performance pitfall).

`pciconf -lvBb` on the host shows the PCI devices and their current driver bindings. A device bound to `ppt(4)` is visible in passthrough mode; a device bound to its native driver is available to the host. This is a quick way to see what is passed through and what is not.

`vmstat -i` on the host shows interrupt counts per device. If a device is passed through to a guest, its interrupts are delivered to the guest's virtual interrupt controller, not to the host. On the host side, you will see the hypervisor's posted-interrupt or interrupt-remapping counts increase instead. This is a subtle but useful diagnostic.

None of this is required reading for driver authorship. It is mentioned here so that when you encounter `bhyve(8)` on a host while debugging a driver, you know where to look first.

### A Typical bhyve Command Line

For readers who want to see what `bhyve(8)` actually looks like when invoked without a wrapper tool, here is a representative command line. It starts a guest named `guest0` with two VCPUs, two gigabytes of memory, a virtio-blk disk backed by a file, and a virtio-net network interface bridged to the host:

```sh
bhyve -c 2 -m 2G \
    -s 0,hostbridge \
    -s 2,virtio-blk,/vm/guest0/disk0.img \
    -s 3,virtio-net,tap0 \
    -s 31,lpc \
    -l com1,stdio \
    guest0
```

Each `-s` flag defines a PCI slot. Slot zero is the host bridge; slot two is a virtio-blk device backed by a disk image file; slot three is a virtio-net device backed by a `tap(4)` interface the host has configured; slot thirty-one is the LPC bridge used for legacy devices like the serial console. The `-l com1,stdio` redirects the guest's first serial port to the host's standard I/O, which is convenient for console access.

When this command runs, the host kernel creates a new VM through `vmm(4)`, allocates memory for the guest's RAM, and hands off VCPU execution to the guest's kernel. The virtio-blk backend in `bhyve(8)` (user-space code) services the guest's block requests by reading and writing the disk image file. The virtio-net backend sends and receives packets through the `tap0` interface, which the host's network stack handles as an ordinary interface.

A driver running inside this guest sees a PCI virtio-blk device, a PCI virtio-net device, and the usual assortment of emulated LPC devices. From the driver's perspective, there is no clue that the "hardware" is implemented by a user-space program running a few milliseconds away on the same host. The abstraction is, by design, complete.

### Inside the vmm(4) Module

`vmm(4)` is the kernel-side infrastructure for `bhyve(8)`. In FreeBSD 14.3, its main source files live in `/usr/src/sys/amd64/vmm/`; an arm64 port is under active development and is expected to ship in a later release, so if you are on FreeBSD 14.3 the amd64 tree is the one to read. The module exports a small control interface through `/dev/vmmctl` and a per-VM interface through `/dev/vmm/NAME`.

The control interface is used by `bhyvectl(8)` and `bhyve(8)` to create, destroy, and enumerate virtual machines. The per-VM interface is used to read and write VCPU state, map guest memory, inject interrupts, and receive VM-exit events from the guest.

When a guest's VCPU executes code that requires emulation (reading from an I/O port, accessing a memory-mapped device register, executing a hypercall), the hardware traps the execution and the control returns to `vmm(4)`. The module either handles the exit in the kernel (for a small set of performance-critical cases, like reading the local APIC) or forwards it to `bhyve(8)` in user space (for most cases, including all VirtIO device emulation).

The kernel/user-space split is a deliberate design choice. Keeping the code in user space makes it easier to develop, debug, and audit. Keeping performance-critical paths in the kernel keeps the overhead low. For FreeBSD, the split has worked well, and `bhyve(8)` has grown from a small academic prototype to a production-quality hypervisor.

For a driver author who is not extending `bhyve(8)` itself, none of this matters in detail. What matters is the observable behaviour: a guest executes code, some operations trap, the hypervisor emulates them, and the guest continues. The driver in the guest sees consistent behaviour regardless of where the emulation happens.

### Nested Virtualisation

A brief mention of nested virtualisation, since it comes up. FreeBSD's `bhyve(8)` on amd64 currently does not support running hardware-virtualised guests inside hardware-virtualised guests (no `VIRTUAL_VMX` or `VIRTUAL_SVM` extensions yet). If you try to run `bhyve(8)` inside a `bhyve(8)` guest, the inner hypervisor will fail to initialise. Intel and AMD both support nested virtualisation in hardware, but the FreeBSD implementation has not yet enabled it.

This matters for labs only in the sense that you must run `bhyve(8)` on bare metal (or on a host that provides nested virtualisation, which some cloud platforms do). If your lab machine is itself a VM, you may find that `bhyve(8)` either fails to load `vmm.ko` or loads it but refuses to start guests.

On arm64, nested virtualisation is a different story; the ARM architecture has cleaner support, and the FreeBSD arm64 port is moving toward enabling it. For up-to-date information, consult the `bhyve(8)` and `vmm(4)` manual pages on the version of FreeBSD you are running.

### An Example: Debugging a PCI Passthrough Driver

To make the host-side discussion concrete, consider a scenario that combines several of the ideas above. You have a PCI network card that your host's driver knows how to drive. You want to pass it through to a `bhyve(8)` guest and test that the same driver works unchanged in the guest.

Step 1: Identify the device. `pciconf -lvBb` shows `em0@pci0:2:0:0` with the driver `em` attached. The device is an Intel Gigabit Ethernet controller.

Step 2: Mark it for passthrough. Edit `/boot/loader.conf` and add `pptdevs="2/0/0"`. Reboot.

Step 3: Verify. After reboot, `pciconf -l` shows `ppt0@pci0:2:0:0`, with `ppt` (the placeholder driver) attached instead of `em`. The device is no longer available to the host's network stack.

Step 4: Configure the guest. In the guest's `bhyve` configuration, add `-s 4,passthru,2/0/0`. This tells `bhyve(8)` to pass through the device to the guest at PCI slot 4.

Step 5: Start the guest. Inside the guest, run `pciconf -lvBb`. The device appears, with its real Intel vendor and device IDs, attached to `em`. Check `dmesg`. The guest's `em` driver has attached to the passthrough device exactly as it would on bare metal.

Step 6: Exercise the device. Configure the interface (`ifconfig em0 10.0.0.1/24 up`), send traffic, verify it works.

Step 7: Shut down the guest. Back on the host, decide whether to keep the device in passthrough mode or return it to the host. If you want it back, edit `/boot/loader.conf` to remove `pptdevs`, reboot, and verify that `em` is attached again.

Every step in this workflow is something the administrator does; the driver itself is untouched. That is the point. If the driver is written correctly (with `bus_dma`, clean detach, no hidden platform assumptions), it works in both environments without changes.

### Wrapping Up

The host side of virtualisation is where FreeBSD plays a slightly different role. Instead of writing a driver that consumes a hypervisor-provided device, you may find yourself writing (or at least interacting with) the infrastructure that provides devices to a hypervisor. `bhyve(8)`, `vmm(4)`, and `pci_passthru(4)` are the main interfaces to know about. PCI passthrough is the most relevant of these for a driver author, because it exercises the detach-and-reattach lifecycle that a well-written driver already supports.

With guests and hosts both covered, the next major environment in FreeBSD's virtualisation story is the one that does not use a separate kernel at all: jails. Section 6 turns to them, to devfs, and to the VNET framework that extends the jail model into the network stack.

## Section 6: Jails, devfs, VNET, and Device Visibility

Virtualisation and containerization share a goal: they both let one physical machine host several workloads that appear, to their users, to be running on separate machines. The mechanisms they use are dramatically different. A virtual machine runs a complete guest kernel on top of a hypervisor; it has its own memory map, its own device tree, its own everything. A container, in the FreeBSD sense, shares the host's kernel entirely. What it has of its own is a filesystem view, a process table, and (if the administrator sets it up that way) a network stack. FreeBSD's answer to the container question is the jail, and jails have existed in some form since FreeBSD 4.0. For driver authors, jails matter because they change what a process can see and do with respect to devices, without changing the driver code at all.

This section explains how jails interact with devices. It focuses on four topics: the jail model itself, the devfs ruleset system that controls device visibility, the VNET framework that gives jails their own network stacks, and the question every container system eventually has to answer: which processes can reach which drivers.

### What a Jail Is, and What It Is Not

A jail is a subdivision of the host kernel's resources. A process running inside a jail sees a subset of the filesystem (rooted at the jail's root directory), a subset of the process table (only processes in the same jail), a subset of the network (depending on whether the jail has its own VNET), and a subset of the devices (depending on the jail's devfs ruleset). The host kernel is a single, shared kernel; there is no separate guest kernel inside the jail. System calls made inside the jail are executed by the same kernel that executes calls from outside the jail, with jail-specific checks inserted at the right places.

Because the kernel is shared, drivers are shared too. There is no separate copy of a driver inside each jail; there is only the one driver that the kernel loaded at boot time. The jail just controls which of the driver's devices are visible to the jail's processes. A jail that has not been configured to see `/dev/null` will see no `/dev/null`; a jail that has been configured to see `/dev/null` will see the same `/dev/null` that the host sees. No new driver instance; no new softc; just a visibility rule.

This simplicity is both jails' great strength and their great limitation. It means jails are extremely cheap: starting a jail is roughly the cost of running a few system calls, whereas starting a VM is the cost of booting a whole kernel. It also means jails cannot isolate kernel-level failures: a driver bug that panics the host kernel panics every jail running on it. A jail is a policy boundary, not a crash boundary. For many workloads that trade-off is excellent; for others, a VM is the right answer.

### The Kernel's View of Jails

Inside the kernel, a jail is represented by a `struct prison`, defined in `/usr/src/sys/sys/jail.h`. Each process has a pointer to the prison it belongs to, via `td->td_ucred->cr_prison`. Code that wants to check whether a process is inside a jail can compare this pointer against the global `prison0`, which is the root jail (the host itself). If the pointer is `prison0`, the process is on the host; otherwise, it is in some jail.

Several helper functions exist for the common checks a driver might want to do. `jailed(cred)` returns true if the credential belongs to a jail other than `prison0`. `prison_check(cred1, cred2)` returns zero if two credentials are in the same jail (or one is the parent of the other); it returns an error otherwise. `prison_priv_check(cred, priv)` is how privilege checks are extended for jails: a root user inside a jail does not have all privileges that root has on the host, and `prison_priv_check` implements the reduction.

A driver author will usually not need to call any of these directly. The framework calls them on your behalf. When a process opens a `devfs` node, for example, the devfs layer consults the jail's devfs ruleset before handing the file descriptor over. When a process tries to use a privilege-gated feature (like `bpf(4)` or `kldload(2)`), the privilege check goes through `prison_priv_check`. A driver only needs to be aware that these checks exist, and to call the framework helpers correctly when it defines its own access rules.

### devfs: The Filesystem Through Which Devices Are Exposed

In FreeBSD, devices are exposed through a filesystem called `devfs(5)`. Every entry under `/dev` is a `devfs` node. A driver that calls `make_dev(9)` or `make_dev_s(9)` creates a `devfs` node; the name, permissions, and uid/gid are attributes of that node. The node is visible in every `devfs` instance that the kernel mounts, and FreeBSD mounts one `devfs` instance per filesystem view: one for the host's `/dev`, one for each jail's `/dev` (if the jail has its own `/dev`), and one per chroot that mounts its own `devfs`.

This is the first part of the visibility story. Each jail (more precisely, each filesystem view) has its own `devfs` mount, and the kernel can apply different rules to different mounts. The rules are called devfs rulesets, and they are the main tool for controlling which devices a jail can see.

### devfs Rulesets: Declaring What a Jail Can See

A devfs ruleset is a numbered set of rules stored in the kernel. Rules can hide nodes, reveal nodes, change their permissions, or change their ownership. The ruleset is applied to a mounted `devfs` instance; every time a lookup happens in that instance, the kernel walks the ruleset and applies the matching rule to each node.

On a fresh FreeBSD system, four rulesets are predefined in `/etc/defaults/devfs.rules` (which is processed when the kernel starts). The file uses `devfs(8)` syntax, a small declarative language for ruleset construction. Let us look at a representative slice.

```text
[devfsrules_hide_all=1]
add hide

[devfsrules_unhide_basic=2]
add path log unhide
add path null unhide
add path zero unhide
add path crypto unhide
add path random unhide
add path urandom unhide

[devfsrules_jail=4]
add include $devfsrules_hide_all
add include $devfsrules_unhide_basic
add include $devfsrules_unhide_login
add path zfs unhide

[devfsrules_jail_vnet=5]
add include $devfsrules_hide_all
add include $devfsrules_unhide_basic
add include $devfsrules_unhide_login
add path zfs unhide
add path 'bpf*' unhide
```

Rule 1, `devfsrules_hide_all`, hides everything. On its own it is useless, because a `devfs` mount with nothing visible is not helpful. It is the starting point for other rulesets.

Rule 2, `devfsrules_unhide_basic`, unhides a small set of essential devices: `log`, `null`, `zero`, `crypto`, `random`, `urandom`. These are the devices that essentially every program needs; without them, even basic tools fail.

Rule 4, `devfsrules_jail`, is the ruleset intended for non-VNET jails. It starts by including `devfsrules_hide_all` (so everything is hidden), then layers `devfsrules_unhide_basic` on top (so the essentials are visible), and adds ZFS devices. The result is a jail that sees a small, safe set of devices and nothing else.

Rule 5, `devfsrules_jail_vnet`, is the equivalent for VNET jails. It is the same as rule 4 with the addition that `bpf*` devices are unhidden, because a VNET jail might legitimately need `bpf(4)` (for tools like `tcpdump(8)` or `dhclient(8)`).

When creating a jail, the administrator specifies which ruleset to apply to the jail's `/dev` mount, either through `jail.conf(5)` (`devfs_ruleset = 4`) or on the command line (`jail -c ... devfs_ruleset=4`). The kernel applies the ruleset to the mount, and the jail sees only what the ruleset allows.

### Creating a Custom devfs Ruleset

For most jails, the default rulesets are enough. When they are not, an administrator can define new ones. A new ruleset must have a unique number (other than the reserved defaults) and can be constructed by inclusion, addition, and override.

A classical example is a jail that needs one specific device that the default rules hide. Suppose we have a jail that runs a service that needs `/dev/tun0`, and we want to expose it without opening the whole `/dev/tun*` family. We would create a ruleset like:

```text
[devfsrules_myjail=100]
add include $devfsrules_jail
add path 'tun0' unhide
```

And apply it in `jail.conf(5)`:

```text
myjail {
    path = /jails/myjail;
    devfs_ruleset = 100;
    ...
}
```

The `devfs(8)` tool can load this ruleset into the running kernel with `devfs rule -s 100 add ...`, or the administrator can edit `/etc/devfs.rules` and restart `devfs`. For persistent configuration, the file is the right place.

### What devfs Rulesets Do Not Do

It is worth noting what devfs rulesets are not. They are not a capability system. Hiding a device from a jail means the jail cannot open that specific path, but a jail that has the `allow.raw_sockets` privilege can still send arbitrary raw packets, ruleset or no ruleset. Hiding `/dev/kmem` does not prevent a determined attacker with the right privileges from reading kernel memory via other means; it just removes one obvious path.

Rulesets are a visibility policy, layered on top of the standard UNIX permissions model. The UNIX permissions still apply: a file hidden by the ruleset is not openable, but a file visible to the ruleset still respects its own permissions. A jail's `root` user can open `/dev/null` because the ruleset says so and the permissions say so, not because the ruleset alone grants access.

For strong isolation, combine rulesets with privilege restrictions (`allow.*` parameters in `jail.conf(5)`) and, if the workload warrants it, with a VM. Rulesets alone are one layer of defence, not the only one.

### A Driver Author's View of devfs Rulesets

For a driver author, devfs rulesets matter for two reasons.

First, when you create a `devfs` node with `make_dev(9)`, you pick a default owner, group, and permission. These apply to every `devfs` view that the node appears in. If your device is something that jails should generally not see (for example, a low-level hardware-management interface), consider whether the name should be obvious (so administrators can easily write a rule that hides it) or whether it should be under a subdirectory (so a single rule can hide the whole subdirectory).

Second, if your driver's device is something that jails commonly need, document that fact in your driver's manual page. The administrator who writes a jail's ruleset is usually not the person who wrote the driver, and they need to know whether to unhide your node. A line like "This device is typically used inside jails; unhide it with `add path mydev unhide` in the jail's ruleset" is very helpful.

Neither of these is a code change. Both are decisions about naming and documentation. Driver authorship is not just about code; it is also about making the code usable by the administrators who will deploy it.

### Wrapping Up the Jail and devfs Side

Jails are the lightweight containerization mechanism in FreeBSD. They share the host kernel, and thus share drivers, but they control which devices are visible through the devfs ruleset mechanism. For a driver author, the relevant design decisions are at the naming and documentation level: pick names that make it easy to write rules, and document which jails should see the device.

The network side of jails is a story of its own, because FreeBSD has two models: single-stack jails that share the host's network, and VNET jails that have their own network stacks. The VNET model is the more interesting one for driver authors, because it has direct consequences for how network drivers are assigned and moved. We turn to that next.

### The VNET Framework: One Kernel, Many Network Stacks

The default jail model has one network stack: the host's. Every jail sees the same routing table, the same interfaces, the same sockets. A jail can be restricted to particular IPv4 or IPv6 addresses (via `ip4.addr` and `ip6.addr` in `jail.conf(5)`), but it cannot have a genuinely independent network configuration. That is often enough for simple workloads, but it is not enough for any jail that wants to run its own firewall, use its own default gateway, or be reached by the outside world through a unique set of addresses on its own interfaces.

VNET (short for "virtual network stack") solves this. It is a kernel feature that replicates the parts of the network stack per jail, so each VNET jail sees its own routing table, its own interface list, its own firewall state, and its own socket namespace. The stack code still belongs to the same kernel, but many of its global variables are now per-VNET instead of truly global. A network driver's per-interface state belongs to whichever VNET it is currently assigned to, and interfaces can be moved from one VNET to another.

For driver authors, VNET is interesting in three respects. It changes how global state in network subsystems is declared. It adds a lifecycle to interfaces: interfaces can be moved, and drivers must support the move cleanly. And it interacts with jail creation and destruction through VNET-specific hooks.

### Declaring VNET State: VNET_DEFINE and CURVNET_SET

VNET's design puts the work on whoever declares global state in a VNET-aware subsystem. Instead of a plain `static int mysubsys_count;`, a VNET-aware declaration looks like:

```c
VNET_DEFINE(int, mysubsys_count);
#define V_mysubsys_count VNET(mysubsys_count)
```

The `VNET_DEFINE` macro expands to a storage declaration that places the variable in a special section of the kernel. At VNET creation time, the kernel allocates a new per-VNET region of memory and initialises it from the section. The `VNET(...)` macro, used via a short alias like `V_mysubsys_count`, resolves to the correct copy for the current VNET.

"The current VNET" is thread-local context. When a thread enters code that operates on a VNET, it calls `CURVNET_SET(vnet)` to establish the context, and `CURVNET_RESTORE()` to tear it down. Inside the context, `V_mysubsys_count` resolves to the right instance. Outside the context, accessing `V_mysubsys_count` is a bug; the macro depends on the thread-local current-VNET pointer, and without that pointer set the result is undefined.

Most driver authors do not need to write `VNET_DEFINE` declarations themselves. The network stack and the ifnet framework declare their own VNET state. Driver-level state (per-interface softcs, per-hardware private data) is not usually VNET-scoped, because it is tied to the hardware, not to the network stack. The driver's state lives wherever the driver put it, and the framework takes care of moving the right bits between VNETs when interfaces move.

What driver authors do need to do is wrap any code that touches network-stack objects in a `CURVNET_SET` / `CURVNET_RESTORE` pair if that code is called from outside a network-stack entry point. Most driver code is called from the network stack already, so the VNET context is already set. The exception is callouts and taskqueues: a callback fired from a callout does not inherit a VNET context, and the driver must establish one before touching any `V_` variable.

A typical pattern inside a callout handler:

```c
static void
mydev_callout(void *arg)
{
    struct mydev_softc *sc = arg;

    CURVNET_SET(sc->ifp->if_vnet);
    /* code that touches network-stack variables */
    CURVNET_RESTORE();
}
```

The driver stored a reference to the interface's VNET on attach (`sc->ifp->if_vnet` is filled in by the framework when the ifnet is created). On every callout, it establishes the context, does its work, and restores. This is one of the few places where driver authors encounter VNET directly.

### if_vmove: Moving an Interface Between VNETs

When a VNET jail starts, it typically is given one or more network interfaces to use. There are two common mechanisms. The first is that the administrator creates a virtual interface (an `epair(4)` or `vlan(4)`) and moves one end into the jail. The second is that the administrator moves a physical interface into the jail outright, so that the jail has exclusive access to it while it runs.

The move is implemented by the kernel function `if_vmove()`. It takes an interface and a destination VNET, detaches the interface from the source VNET's network stack (without destroying it), and reattaches it to the destination VNET's network stack. The interface retains its driver, its softc, its hardware state, and its configured MAC address. What changes is which VNET's routing table, firewall, and socket namespace it is attached to.

For a driver author, the move imposes a lifecycle requirement. The interface must be able to survive being detached from one VNET and attached to another. The driver's `if_init` function may be called again in the new context. The driver's `if_transmit` function may receive packets from the new VNET's sockets. Any state the driver caches about the "current" network stack (for example, routing-table lookups) must be invalidated or re-established.

For a network driver written to the standard `ifnet(9)` interface, the move usually works without special handling. The ifnet framework does the heavy lifting, and the driver is largely unaware. What the driver must avoid is holding references to VNET-scoped state across entry points. Code like "grab a pointer to the current routing table at attach and cache it" does not survive an interface move, because the interface may later belong to a different VNET with a different table.

A related primitive, `if_vmove_loan()`, is used for interfaces that should return to the host when the jail shuts down. The jail gets the interface on a loan basis, and on jail destruction the interface is moved back. This is common for `epair(4)` setups where the physical connection (if any) belongs to the host and only the logical presence belongs to the jail.

### VNET Lifecycle Hooks

When a VNET is created or destroyed, subsystems that keep VNET state need to initialise it or release it. The `VNET_SYSINIT` and `VNET_SYSUNINIT` macros register functions to be called at those moments. A network protocol might register an init function that creates per-VNET hash tables, and an uninit function that destroys them.

Driver authors rarely need these hooks. They are relevant to protocols and stack features, not to device drivers. They are mentioned here because you will see them scattered across the network stack code, and knowing that they are VNET lifecycle hooks helps you read the source.

### A Concrete VNET Pattern

To make the VNET abstractions concrete, consider a simplified pseudo-driver that keeps a counter of packets received per VNET. The counter must be per-VNET because the same pseudo-driver might be cloned into several VNETs simultaneously, and each clone should have its own count.

The declaration looks like:

```c
#include <net/vnet.h>

VNET_DEFINE_STATIC(uint64_t, pseudo_rx_count);
#define V_pseudo_rx_count VNET(pseudo_rx_count)

/* Optional init for the counter */
static void
pseudo_vnet_init(void *unused)
{
    V_pseudo_rx_count = 0;
}
VNET_SYSINIT(pseudo_vnet_init, SI_SUB_PSEUDO, SI_ORDER_ANY,
    pseudo_vnet_init, NULL);
```

The `VNET_DEFINE_STATIC` places the counter in a per-VNET section of the kernel image. When a new VNET is created, the kernel copies the per-VNET section into fresh memory, so each VNET starts with its own zero-initialised copy of the counter. The `V_pseudo_rx_count` shorthand is a macro that expands to `VNET(pseudo_rx_count)`, which in turn dereferences the current VNET's storage.

When a packet arrives, the receive path increments the counter:

```c
static void
pseudo_receive_one(struct mbuf *m)
{
    V_pseudo_rx_count++;
    /* deliver packet to the stack */
    netisr_dispatch(NETISR_IP, m);
}
```

This looks like ordinary code, because the macro hides the per-VNET indirection. The condition for it to be correct is that the thread is already in the right VNET context when `pseudo_receive_one` is called. In a network driver's receive path that condition is automatic: the network stack calls the driver's entry point with the right context already established.

When the counter is accessed from an unusual context, the context must be established explicitly:

```c
static void
pseudo_print_counter(struct vnet *vnet)
{
    uint64_t count;

    CURVNET_SET(vnet);
    count = V_pseudo_rx_count;
    CURVNET_RESTORE();
    printf("pseudo: vnet %p has received %lu packets\n", vnet, count);
}
```

Here the function is called from some administrative path that does not know the current VNET, so it sets the context manually, reads the counter, restores the context, and prints the result. This is the pattern you will see repeated in VNET-aware code.

### Reading Real VNET Code

If you want to see the pattern in a real driver, `/usr/src/sys/net/if_tuntap.c` is a good starting point. The `tun` and `tap` cloning drivers are VNET-aware: each clone belongs to one VNET, and creating or destroying clones respects the VNET boundaries. The code is well-commented and small enough to read in a couple of evenings.

Two patterns in `if_tuntap.c` are worth noticing. The first is the use of `V_tun_cdevsw` and `V_tap_cdevsw`, per-VNET character device switch structures. Each VNET has its own copy of the switch, so `/dev/tun0` in one VNET can map to a different underlying clone than `/dev/tun0` in another VNET. This is the kind of fine-grained per-VNET duplication that the framework enables.

The second is the use of `if_clone(9)` with VNET. The `if_clone_attach` and `if_clone_detach` functions take VNET into account automatically, so a clone created in a VNET lives in that VNET until it is explicitly moved or destroyed. The cloner does not need to carry VNET state in its softc; the framework handles it.

Studying these patterns makes the text in this chapter concrete. Read, take notes, and come back to the text if anything is unclear.

### Hierarchical Jails

A brief mention of hierarchical jails, which are a feature some readers will encounter. FreeBSD supports nesting jails: a jail can create child jails, and the child jails are bounded by the parent jail's restrictions. This is useful for services that want to further subdivide their environment.

From a driver author's perspective, hierarchical jails do not introduce new APIs. The `prison_priv_check` helper walks the hierarchy automatically: a privilege is granted only if every level of the hierarchy allows it. A driver that uses the framework correctly works in hierarchical jails without additional code.

The administrative side is more complex (the parent jail must allow child-jail creation, the children inherit a restricted set of privileges), but the driver-side does not need to care. Knowing that the feature exists helps when you see nested jails in a deployment.

### Putting It All Together

A FreeBSD system with jails and VNET is a system where a single kernel serves many isolated environments. Each environment sees its own filesystem view, its own process table, its own devices (filtered by devfs ruleset), and possibly its own network stack (under VNET). The driver serving all of them is a single shared binary, but it respects the isolation because it calls the framework APIs correctly.

The framework APIs for this isolation, `priv_check`, `prison_priv_check`, `CURVNET_SET`, `VNET_DEFINE`, and the cloning helpers, are small and self-contained. A driver author who learns them once can write drivers that work correctly in every jail configuration the administrator can dream up. There is no need to special-case specific jail setups; the framework does that work.

### Single-Stack Jails and the In-Between

Not every jail needs VNET. A jail that is running a web server that talks through a reverse proxy on the host may do perfectly well with a single-stack jail bound to a specific IPv4 address. The main cost of VNET is complexity in the stack (every protocol must be VNET-aware) and some memory overhead (each VNET has its own hash tables, caches, and counters). For lightweight jails, the single-stack model is often the better choice.

The trade-off for driver authors is worth knowing. A network driver that works correctly in a host-stack jail may still need attention for VNET jails, because the interface can be moved under VNET but not under the single-stack model. Writing the driver with VNET in mind from the start is the right approach; the additional discipline is small, and it future-proofs the driver.

### Wrapping Up

Jails share the host's kernel and thus the host's drivers. What they do not share is visibility: devfs rulesets control which device nodes a jail can open, and VNET controls which network interfaces a jail can use. Driver authors benefit from understanding both mechanisms, because the design choices they make (how to name `devfs` nodes, how to handle VNET context in callouts, how to support interface moves) affect how their driver behaves in jailed environments.

With the jail picture complete, we can now think about the companion question: once a jail has access to a device, what privileges does it have to use that device? Section 7 takes up resource limits and security boundaries, and looks at the other side of the jail policy story.

## Section 7: Resource Limits, Security Boundaries, and Host-Versus-Jail Access

A driver is not an isolated object. It is a consumer of kernel resources and a provider of services to processes, and both relationships are mediated by the kernel's security and accounting frameworks. When a driver runs on a FreeBSD host that contains jails, the security boundary shifts: some privileges that are unconditional on the host are restricted for jail processes, and some resources that are unmetered on a traditional system are now subject to per-jail limits. A good driver author knows where those boundaries are, because their driver's behaviour on a host is not always its behaviour inside a jail.

This section covers three topics. First, the privilege framework and how `prison_priv_check` reshapes it for jails. Second, `rctl(8)` and how resource limits apply to kernel resources a driver might care about. Third, the practical distinction between attaching a driver from inside a jail (which is usually impossible) and making a driver's services available to a jail (which is the usual case).

### The Privilege Framework and prison_priv_check

FreeBSD uses a privilege system to make fine-grained decisions about what a process can and cannot do. Traditional UNIX has a single privilege bit (root versus not-root), and that bit determines everything. FreeBSD refines this with the `priv(9)` framework, which defines a long list of named privileges. Each privilege covers a specific kind of operation. Loading a kernel module is `PRIV_KLD_LOAD`. Setting a process's root directory is `PRIV_VFS_CHROOT`. Opening a raw socket is `PRIV_NETINET_RAW`. Configuring an interface's MAC address is `PRIV_NET_SETLLADDR`. Using a BPF device for packet capture is `PRIV_NET_BPF`.

A process that is root (uid 0) has all of these privileges on the host. A process that is a jail's root has some of them, but not all. The restriction is handled by `prison_priv_check(cred, priv)`: it takes the credential and the privilege name, and returns zero if the privilege is granted and an error (usually `EPERM`) if it is denied. The kernel's privilege-checking path is structured so that for a jailed credential, `prison_priv_check` is called first; if it denies the privilege, the caller returns `EPERM` without further ado.

Which privileges a jail is allowed to exercise is determined by two things. The first is a hardcoded list inside `prison_priv_check`: some privileges are simply never granted to jails, regardless of configuration. Examples include `PRIV_KLD_LOAD` (loading kernel modules) and `PRIV_IO` (I/O port access). The second is the `allow.*` parameters in `jail.conf(5)`, which turn on or off specific categories. `allow.raw_sockets` (off by default) controls `PRIV_NETINET_RAW`. `allow.mount` (off by default) controls filesystem mounting privileges. `allow.vmm` (off by default) controls access to `vmm(4)` for running nested hypervisors. The defaults err on the side of denial: if you do not explicitly allow it, the jail does not get it.

For a driver author, the privilege framework matters whenever the driver does something that a process might or might not be allowed to do. A driver that implements a low-level hardware interface might require `PRIV_DRIVER` (the catch-all for driver-specific privilege checks) or a more specific privilege. A driver that exposes a character device whose `ioctl`s can reconfigure the hardware will call `priv_check(td, PRIV_DRIVER)` (or a more specific name) to decide whether the caller is allowed to do the reconfiguration.

The standard pattern in driver code looks like this:

```c
static int
mydev_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int fflag,
    struct thread *td)
{
    int error;

    switch (cmd) {
    case MYDEV_CMD_RECONFIGURE:
        error = priv_check(td, PRIV_DRIVER);
        if (error != 0)
            return (error);
        /* do the reconfiguration */
        break;
    ...
    }
}
```

`priv_check(td, PRIV_DRIVER)` does the right thing for both host and jail callers. On a host, a root process passes; on a host, a non-root process is denied (unless the driver grants permission by other means). Inside a jail, `prison_priv_check` is consulted, and by default `PRIV_DRIVER` is denied inside jails. If the administrator has configured the jail to allow driver access (a very unusual setting), the privilege is granted and the call proceeds.

The result is that a driver that uses `priv_check` correctly gets jail safety for free. The driver does not need to know whether the caller is in a jail; it just asks whether the caller has the right privilege, and the framework takes care of the rest.

### Named Privileges Most Relevant to Drivers

A short reference for some of the privileges a driver author will encounter.

`PRIV_IO` is for direct I/O port access on x86. It is defined in `/usr/src/sys/sys/priv.h` and is denied to jails unconditionally. Drivers that offer raw I/O port access to user space are rare (usually limited to legacy hardware like `/dev/io`), but when they exist, they use this privilege.

`PRIV_DRIVER` is the catch-all for driver-specific privileges. If a driver needs to gate an `ioctl` that only an administrator should call, `PRIV_DRIVER` is the default choice.

`PRIV_KMEM_WRITE` gates write access to `/dev/kmem`. Like `PRIV_IO`, it is denied to jails. Writing to kernel memory is the ultimate privileged operation; no reasonable container policy allows it.

`PRIV_NET_*` is a family of network-related privileges. `PRIV_NET_IFCREATE` is for creating network interfaces; `PRIV_NET_SETLLADDR` is for changing MAC addresses; `PRIV_NET_BPF` is for opening a BPF device. Each has its own jail policy, and the combinations are how a VNET jail can (for example) run `dhclient(8)` (which needs `PRIV_NET_BPF`) without also being able to arbitrarily reconfigure interfaces.

`PRIV_VFS_MOUNT` gates filesystem mounting. Jails have a very restricted version of this by default: they can mount `nullfs` and `tmpfs` if `allow.mount` is set, but not arbitrary filesystems.

A complete list is in `/usr/src/sys/sys/priv.h`. For driver authorship, you will rarely invent new privilege categories; you will pick the existing one that fits.

### rctl(8): Per-Jail and Per-Process Resource Limits

Jails (and, indeed, processes) can be subject to resource limits beyond the traditional UNIX `ulimit(1)` model. FreeBSD's `rctl(8)` (the runtime resource control framework) lets an administrator set limits on a wide variety of resources, and enforce them with specified actions when the limits are hit.

The limits cover things like memory use, CPU time, number of processes, number of open files, I/O bandwidth, and so on. They can be applied per user, per process, per login class, or per jail. The typical use in a jail setup is to cap a jail's total memory and CPU so that a misbehaving application inside the jail cannot affect other jails on the same host.

For a driver author, `rctl(8)` matters for a subtle reason. Drivers allocate resources on behalf of processes. When a driver calls `malloc(9)` to allocate a buffer, the memory goes somewhere. When a driver creates a file descriptor by opening a file internally, the descriptor goes somewhere. When a driver spawns a kernel thread, the thread runs as part of somebody's accounting. If the "somebody" is a process inside a jail, the accounting might hit the jail's `rctl` limits.

Usually this is exactly what you want. If a jail is supposed to be limited to 100 MB of memory, and an allocation on behalf of a jail process should count against that limit, and `rctl` hits the limit, the allocation should fail with `ENOMEM`. Your driver then propagates the failure back to user space, and the well-behaved application inside the jail handles it.

Occasionally the accounting is less obvious. A driver that maintains a pool of buffers shared across all callers will, by default, charge the pool to the kernel rather than to any single process. That is fine, but it means the pool is unmetered: a very active jail can consume a larger share of the pool than it "should" under the resource limits. For most drivers this is acceptable, but for drivers whose resources are expensive (large DMA buffers, for instance) it may be worth considering whether the resource should be tracked per-process through `racct(9)`, the underlying accounting layer that `rctl` sits on top of.

The `racct(9)` framework exposes `racct_add(9)` and `racct_sub(9)` functions for drivers that want to participate in accounting. Most drivers never call these directly. Adding `racct` support is a deliberate design choice, usually made when a driver's resource consumption is large enough to matter in aggregate. For everyday character-device drivers or network drivers, the default accounting done by the kernel (per-socket buffer memory, per-process file descriptor counts, and so on) is sufficient.

### Enforcement Actions and What They Mean for Drivers

When a resource limit is hit, `rctl(8)` can do one of several things: deny the operation, send a signal to the offending process, log the event, or throttle (for rate-based resources). The enforcement is handled by the kernel's accounting layer, not by drivers. What drivers see is the result: an allocation fails, a signal delivers, a rate-limited operation takes longer.

For a driver, the practical implication is that every allocation and every resource acquisition must be written to handle failure. This is not specific to jails or `rctl`; it is good defensive coding anyway. A `malloc(9)` with `M_WAITOK` can wait for memory indefinitely, but in a jail with a memory limit, it may still fail (if `M_WAITOK` is not set) or it may block a long time waiting for memory that will never be freed (because no other process in the jail has any to free).

The rule of thumb: if your driver is doing allocations on behalf of a user process, consider whether `M_NOWAIT` is more appropriate than `M_WAITOK`, and whether the caller can tolerate a delayed or failed allocation. Jails (and the resource limits around them) make the consideration more than theoretical.

### Host-Side Drivers versus Jail-Side Processes

A recurring question for driver authors is: can my driver be loaded or attached from inside a jail? The short answer is almost always no. Loading kernel modules (`kldload(2)`) requires `PRIV_KLD_LOAD`, which is never granted to jails. A jail that needs access to a driver's services must have that driver already loaded and attached on the host, and then the jail can use the driver through the usual user-space interfaces.

This is a consequence of the single-kernel model. A jail does not have its own kernel, so it cannot load drivers of its own. What it has is access (subject to the devfs ruleset) to the drivers that the host has loaded. In practice this means:

- Driver loading and unloading happen on the host. The host administrator is responsible for `kldload` and `kldunload`.
- Device attachment to devices happens on the host. The driver's `DEVICE_ATTACH` runs in host context, not in jail context.
- Device access from user space happens inside the jail, through `/dev` (if the ruleset allows) and through the driver's `open`/`read`/`write`/`ioctl` methods.

The separation is usually clean. The driver does not need to know whether its caller is in a jail; the kernel handles the context. Where it sometimes matters is in `ioctl` handlers that want to distinguish host and jail callers, or in drivers that allocate resources per-open whose release policy differs.

A specific caveat: when a driver creates state on open, that state survives until close. If the jail that held the open file descriptor goes away before the descriptor is closed (because the jail was destroyed while processes still had the device open), the kernel will close the descriptor on the jail's behalf. The driver's `close` method will run in a safe context. But the driver should not assume that the jail still exists during close; it may not, and if the driver tries to reach into the jail's state, it will encounter a freed `struct prison`. The clean rule is that `close` should only touch driver state, not jail state.

### How Drivers Cooperate with Jails in Practice

Putting the pieces together, a typical FreeBSD setup with a driver and a jail looks like this.

1. The administrator loads the driver on the host, either at boot through `/boot/loader.conf` or at runtime through `kldload(8)`.
2. The driver's probe and attach functions run on the host, create whatever `devfs` nodes are appropriate, and register their `cdev_methods`.
3. The administrator creates a jail, possibly with a specific devfs ruleset and possibly with VNET.
4. The jail's processes open the driver's devices (if they are visible through the ruleset) and make `ioctl` or `read`/`write` calls.
5. The driver's methods execute on behalf of the jail process, with the jail's credential attached to the thread, and the driver's `priv_check` calls correctly return `EPERM` for privileges the jail does not hold.
6. When the jail is destroyed, open file descriptors are closed, the driver's `close` methods run cleanly, and the driver's state returns to its normal host-only view.

Nothing in this flow requires the driver to know about jails explicitly. The driver is a passive participant that calls the right framework functions, and the framework handles the rest. This is the cleanest possible design, and it is what you should aim for.

### The Container Frameworks: ocijail and pot

FreeBSD's jail infrastructure is a kernel mechanism; the user-space tooling around it has multiple forms. The base system provides `jail(8)`, `jail.conf(5)`, `jls(8)`, and related tools. These are enough to manage jails by hand or with shell scripts.

Higher-level container frameworks have emerged on top. `ocijail` aims to provide an OCI (Open Container Initiative) runtime that uses jails as the isolation mechanism, letting FreeBSD participate in container ecosystems that use OCI-compliant images. `pot` (available from ports) is a more FreeBSD-native container manager that bundles a jail with a filesystem layer, a network configuration, and a lifecycle. Both are external to the base system and are installed through the ports collection or packages.

For a driver author, these frameworks do not change the fundamentals. They still use jails underneath; they still rely on devfs rulesets and VNET for isolation; they still respect the same privilege framework. What they change is how administrators describe and deploy the containers, not how the drivers interact with them. A driver that works with a hand-crafted `jail.conf(5)` will work with `ocijail` and `pot` as well.

The most a driver author usually needs to know is that these frameworks exist and are becoming common. If your driver's documentation mentions jails, mention that the recommendations apply equally to container frameworks built on top. That single sentence saves administrators a lot of guesswork.

### Wrapping Up

Jails are a policy boundary around a shared kernel. The policy extends into three dimensions: which devices are visible (devfs rulesets), which privileges are granted (`prison_priv_check` and `allow.*` parameters), and which resources can be consumed (`rctl(8)`). Driver authors encounter each of these in small, local ways: `priv_check` for privileged operations, sensible naming of `devfs` nodes, graceful handling of allocation failures. There is no large new API to learn; there are small new habits to pick up.

With the security and resource picture covered, the final conceptual topic is how to actually test and develop drivers in virtualised and containerised environments. Section 8 pulls the chapter's ideas together into a development workflow.

## Section 8: Testing and Refactoring Drivers for Virtualised and Containerised Environments

A driver that runs on bare metal is a driver that works on one configuration. A driver that runs across virtualisation and containerisation has been exercised under varied conditions: different bus presentations, different interrupt delivery mechanisms, different memory-mapping behaviours, different privilege contexts. The driver that makes it through all of those without changes is the driver that will survive the next new environment too. This section describes the development and testing workflow that gets you there.

The workflow has three layers. The development layer uses a VM as a disposable kernel host, so that panics and hangs cost you nothing. The integration layer uses VirtIO devices, passthrough, and jails to exercise the driver in realistic environments. The regression layer uses automation to run the whole suite repeatedly as the driver evolves.

### Using a VM as Your Development Host

When you are writing a kernel module, the cost of a panic is your session. On a bare-metal development machine, a panic interrupts your work, possibly forces a filesystem check, and may require a reboot with manual recovery steps. On a VM, a panic is a detail: the VM stops, you restart it, and the host is untouched.

For this reason, experienced driver authors do almost all of their new-driver development inside a `bhyve(8)` or QEMU-based VM, not on bare metal. The workflow looks like this:

1. A FreeBSD 14.3 VM is installed in `bhyve(8)` with a standard disk image.
2. The source tree is either on the VM's own disk or mounted via NFS from a host-side build machine.
3. The driver is built inside the VM (`make clean && make`) or on the host and copied in.
4. `kldload(8)` loads the module. If the module panics the kernel, the VM crashes, and the VM is restarted.
5. Once the module loads cleanly, it is exercised against whatever test fixture you have: a VirtIO device, a loopback mode, or a passthrough target.

The key point is that iteration is fast. A broken module that would make a bare-metal machine unbootable is a minor inconvenience in a VM. You can try things that you would never try on a machine you rely on.

For VirtIO driver development specifically, the VM is not just convenient; it is the only sensible platform. VirtIO devices only exist inside VMs (or under qemu emulation), so the VM is where the devices are. Starting a VM with a virtio-rnd device, or a virtio-net device, or a virtio-console device, gives you a target to develop against. The hypervisor provides everything a real device would provide, including interrupts, DMA, and register access, so the driver you write inside the VM is the same driver that will run anywhere else.

### Using VirtIO as a Test Substrate

VirtIO has a second role beyond being the target for VirtIO drivers: it is a test substrate. Because VirtIO devices are easy to define, easy to emulate, and well-documented, they are useful for building controlled test scenarios even for drivers that are not VirtIO drivers.

For example, suppose you are writing a driver for a physical PCI device, and you want to test how your driver handles a specific error condition. On the real hardware, reproducing the error may require a specific physical fault, which is hard to arrange. On a VirtIO-based proxy, you can implement a device that always returns the error, and test your driver's error path without touching the physical hardware. The caveat is that your driver must be loosely coupled to the specific hardware (the techniques from Chapter 29 matter here); the more the driver is split along the accessor/backend lines described there, the easier it is to test the upper layers against a synthetic backend.

The mechanism for synthesizing VirtIO devices in user space is `bhyve`'s own pluggable emulated devices. Writing a new `bhyve` device emulator is beyond the scope of this chapter, but the relevant code lives in `/usr/src/usr.sbin/bhyve/` and is approachable if you have basic C skills. For simpler cases, using a pre-existing virtio-blk, virtio-net, or virtio-console device configured with specific parameters is often enough.

### Using Jails for Integration Testing

When your driver is working and you want to verify it under container-style isolation, jails are the obvious next step. The setup is simple: create a jail with an appropriate devfs ruleset that exposes your device, and run your user-space test harness inside the jail.

A typical test shape:

```text
myjail {
    path = /jails/myjail;
    host.hostname = myjail;
    devfs_ruleset = 100;   # custom ruleset that unhides /dev/mydev
    ip4 = inherit;
    allow.mount = 0;
    exec.start = "/bin/sh /etc/rc";
    exec.stop = "/bin/sh /etc/rc.shutdown";
    persist;
}
```

Inside the jail, you run your test harness. It opens `/dev/mydev`, exercises the `ioctl`s or the `read`/`write` methods, and records results. On the host, you run the same harness and compare. If the jail-side test passes and the host-side test passes, your driver tolerates the jail environment.

If one passes and the other does not, you have a diagnostic opportunity. Possible reasons for a divergence include: a privilege check that denies the jail call (look for `priv_check` in your driver), a device node permission the ruleset does not account for, a resource limit the host happens to avoid, or a jail-specific code path in your driver that exists by mistake. Each of these is fixable once identified.

### Using VNET Jails for Network Driver Testing

For network drivers, the test is similar but uses VNET. Create a jail with `vnet = 1` in `jail.conf(5)`, move one end of an `epair(4)` into the jail, and run traffic between the jail and the host. If your driver is a physical network driver, you can also move the physical interface into the jail for a full-isolation test.

The VNET test exercises the `if_vmove()` lifecycle: the interface is detached from the host's VNET, reattached to the jail's VNET, and eventually returned. A driver that survives this without losing state is a driver that tolerates VNET. A driver that panics, hangs, or stops delivering packets after a move has work to do.

The common failure modes in VNET testing are:

- The driver holds a pointer to the host's ifnet or VNET across the move, and uses it from a callout after the move has happened.
- The driver's `if_init` assumes it is called in the original VNET and fails when called in the new one.
- The driver cleans up incorrectly at `if_detach`, because it does not distinguish "detach to move" from "detach to destroy".

Each of these is diagnosable with `dtrace(1)` or kernel printfs at the right place. The first time you see a VNET-related crash, find the point in the driver where the move happens and work backward from there.

### Passthrough Testing

PCI passthrough is the exercise that validates your driver's detach path. Create a `bhyve(8)` guest with your device passed through, install FreeBSD inside, and load your driver there. If the driver attaches cleanly in the guest, your device setup and DMA code handle IOMMU remapping correctly. If the driver loads, the test is simple: run the driver's normal workload inside the guest.

The detach test is the harder one. Shut down the guest, rebind the device to its native driver on the host (by unloading `ppt(4)` from that device, if necessary, and letting the host's driver re-attach), and exercise the driver on the host. If the driver attaches cleanly after the guest has used the device and put it through whatever state changes, the driver's `DEVICE_ATTACH` is properly defensive. If it fails, look for assumptions about the device's initial state that should not be assumptions.

The full round-trip (host, guest, host) is the gold standard for passthrough compatibility. A driver that passes it can be handed to any administrator with confidence.

### Hypervisor Detection in Tests

If your driver uses `vm_guest` to adjust defaults, test the adjustment. Run the driver on bare metal (if available), inside `bhyve(8)`, inside QEMU/KVM, and observe whether the defaults it picks make sense. The `kern.vm_guest` sysctl is your quick check:

```sh
sysctl kern.vm_guest
```

If your driver logs its environment at attach time ("attaching on bhyve host, defaulting to X"), the log makes the detection visible, which helps with debugging. Do not over-log: once at attach is usually enough.

### Automating the Test Suite

Once the individual tests are known, the next step is to run them repeatedly as the driver evolves. FreeBSD's `kyua(1)` test runner, combined with the `atf(7)` test framework, is the standard mechanism. A test suite that includes a "bare-metal test", a "VirtIO guest test", a "VNET jail test", and a "passthrough test" covers most of what you want to verify.

The details of writing tests in `atf(7)` are outside this chapter; they are treated more fully in Chapter 32 (Debugging Drivers) and Chapter 33 (Testing and Validation). The point for now is that the test suite should exercise the driver across the environments it is expected to run in. A single test on bare metal proves very little about virtualisation; a suite of tests across environments proves something about portability.

### Refactoring Tips Revisited

In Chapter 29 we introduced a discipline for portability: accessor layers, backend abstractions, endian helpers, no hidden hardware assumptions. Virtualisation and containerisation put that discipline to the test. A driver written with clean abstractions will survive the variety of environments described in this chapter; a driver with hidden assumptions will hit them as soon as the environment changes.

The refactoring tips most relevant to this chapter are:

- Put all register access through accessors, so the access path can be simulated or redirected in tests.
- Handle the full lifecycle: attach, detach, suspend, resume. Passthrough exercises attach and detach repeatedly; VNET exercises detach in a way the driver might not see on bare metal.
- Use `bus_dma(9)` and `bus_space(9)` correctly, never physical addresses directly. The guest-versus-host address translation under passthrough depends on correct use of these APIs.
- Use `priv_check` for privilege gating, not hardcoded uid 0 checks. Jail restrictions work correctly only if the framework is called.
- Use `CURVNET_SET` around any callout or taskqueue code that touches network-stack state. This is the one VNET-specific discipline that catches most driver authors off guard.

None of these are new concepts. They are all standard FreeBSD driver practice. What this chapter adds is the context in which each one matters: which environments exercise which disciplines. Knowing that lets you prioritise when deciding what to refactor first.

### A Development Order That Works

Putting the pieces together, here is a development order that has proven effective.

1. Start in a `bhyve(8)` VM. Write the driver's basic skeleton (module hooks, probe and attach, simple I/O path). Exercise it with `kldload` and a minimal test.
2. Add the accessor layer and the backend abstraction from Chapter 29. Test that the simulation backend runs, even if the real hardware is not yet plugged in.
3. If VirtIO is the target, develop against `virtio_pci.c` in the VM. You have a real device to talk to, and you can iterate quickly.
4. If real hardware is the target, begin PCI passthrough testing when the driver reaches a stable point. The round-trip (host, guest, host) becomes part of the regular test cycle.
5. Add jail-based tests when the driver exposes user-space interfaces. Start with a single-stack jail; move to VNET if the driver is a network driver.
6. Add automation with `kyua(1)` and `atf(7)` as the test count grows.
7. When a bug is found, reproduce it in the smallest environment that shows the bug, fix it, and add a regression test at that level.

This order keeps the iteration fast at the beginning (where it matters most) and adds environmental complexity only as the driver stabilises. Trying to test everything at once is a common beginner mistake; it is how projects stall. The incremental path is slower per step but much faster in aggregate.

### An End-to-End Example: From Bare Metal to Passthrough

To illustrate the workflow, here is a complete example walkthrough for a hypothetical driver called `mydev`. The driver is a PCI-based character device; it has a small register interface, uses MSI-X interrupts, and performs DMA. The development order below is condensed into a single narrative so you can see how the steps connect.

Day 1: skeleton in a VM. You install a FreeBSD 14.3 guest in `bhyve(8)`, set up NFS so the source tree on your host is visible in the guest, and write the module skeleton. It is a `KMOD=mydev, SRCS=mydev.c` Makefile and a `mydev.c` with `DECLARE_MODULE`, a stub probe, a stub attach, and a stub detach. It loads and unloads cleanly. `dmesg` shows "mydev: hello" on load.

Day 2: accessor layer. You add the Chapter 29 accessor pattern: all register access goes through `mydev_reg_read32` and `mydev_reg_write32`, with the real backend calling `bus_read_4` and `bus_write_4`. You also add a simulation backend that stores register values in a small in-memory array. The simulation backend is selected by a module parameter. The accessor layer means the same driver can run against a real device or against the simulated backend without changes to the upper code.

Day 3: upper-layer code. You add the driver's core logic: initialisation, the character-device interface (`open`, `close`, `read`, `write`), the `ioctl` surface, and the DMA setup. The simulation backend does not model DMA, but the upper layer is structured to treat DMA through `bus_dma(9)` handles, so the code is written correctly from the start. You exercise the code through the simulation backend in a simple test program: open the device, issue `ioctl`s, verify the responses.

Day 4: real hardware, passthrough setup. You have the target hardware in a workstation. You add `pptdevs` to `/boot/loader.conf`, reboot, and confirm that `ppt(4)` has claimed the device. You add `passthru` to the `bhyve(8)` guest configuration and boot the guest. Inside the guest, you load your driver. The driver attaches to the passed-through device. You have now demonstrated that the real hardware path works.

Day 5: interrupts and DMA testing. The driver receives interrupts; the MSI-X setup code works. You test DMA: a short DMA read works, a long DMA read works, a simultaneous read and write works. You find one bug: the driver programs a physical address computed incorrectly, but only for DMA regions crossing a page boundary. You fix it. Total time spent debugging: two hours, all in a VM that would have required a hard reboot of the workstation on bare metal.

Day 6: jail test. You exit the guest, return to the host, and configure a jail that sees `/dev/mydev0`. Your test program runs inside the jail and exercises the driver exactly as it did on the host. One `ioctl` fails with `EPERM`; you look at the driver and find that you forgot to add a `priv_check` for an operation that should require privilege. You add the check, and now the jail behaves correctly (the ioctl is denied for non-privileged callers; the host's root can still run it).

Day 7: VNET test (if the driver has a network interface). You create a VNET jail and move a cloned interface into it. The interface works. You notice that one of your callouts does not set `CURVNET_SET` before accessing a per-VNET counter; you fix it. The callout now works in both the host VNET and the jail VNET without interference.

Day 8: full round-trip. You destroy the jail, shut down the guest, unload `ppt(4)` from the device (or reboot without `pptdevs`), and wait for the host's driver to re-attach. The attach works cleanly. You exercise the device on the host. It works. The round trip host-guest-host is complete.

This eight-day cycle is a stylised version of real development; your mileage will vary. The important point is that each day's work builds on the previous one, and the test environments become more exacting as the driver stabilises. By day eight, you have exercised the driver under every environment it will see in production, and you have fixed the bugs that each environment exposes. What remains is soak testing and user-facing polish, which are topics for Chapters 33 and 34.

### Measuring Virtualisation Overhead

A quick note on performance. Drivers running under virtualisation sometimes show measurable overhead compared to bare metal. The sources of overhead include VM exits on I/O, interrupt delivery through the hypervisor, and DMA address translation through the IOMMU.

For most drivers most of the time, the overhead is not significant. Modern hardware accelerates almost every part of the virtualisation path (posted interrupts, EPT page tables, SR-IOV), and the fraction of CPU time spent in hypervisor code is typically in the low single digits. For performance-sensitive drivers, though, measuring and understanding the overhead is essential.

The tools for measurement are the same ones you use in other performance work. `pmcstat(8)` samples hardware counters, including counters for VM exits and for translation-lookaside-buffer misses that the IOMMU may cause. `dtrace(1)` can trace specific kernel paths, and with the `fbt` provider you can measure how often each path is entered and how long it takes. `vmstat -i` shows interrupt rates.

For VirtIO drivers, the most common source of overhead is excessive notifications. Every `virtqueue_notify` potentially causes a VM exit, and a driver that notifies on every packet rather than coalescing can generate hundreds of thousands of exits per second. The `VIRTIO_F_RING_EVENT_IDX` feature, if negotiated, lets the guest and host cooperate to reduce notification frequency. Check that your driver negotiates this feature if it runs in a high-packet-rate path.

For passthrough drivers, the most common source of overhead is IOMMU translation misses. Each DMA buffer must be walked through the IOMMU's page tables, and a driver that maps and unmaps buffers many times per second spends a lot of CPU on that. The fix is usually to keep DMA mappings alive for longer (using `bus_dma(9)`'s mapping-retention features) rather than map-and-unmap every transaction.

Performance tuning is a whole chapter of its own (Chapter 34). For now, the takeaway is that virtualisation has measurable costs, those costs are usually small, and the standard FreeBSD tooling applies without modification.

### Wrapping Up

A driver that runs correctly under virtualisation and containerisation is not the result of special virtualisation code. It is the result of standard FreeBSD driver discipline, exercised in environments that expose hidden assumptions. The test and development workflow in this section is the practical side of that discipline: a VM for fast iteration, VirtIO for a controlled test substrate, jails for privilege and visibility checks, VNET for network-stack testing, passthrough for hardware round-trips, and automation to keep the whole thing honest.

With the conceptual and practical material in place, the rest of the chapter turns to hands-on labs that let you try these techniques yourself. Before we get there, two more sections complete the picture. Section 9 covers the quieter but equally important topics of time, memory, and interrupt handling under virtualisation, the areas where beginner drivers often fail in subtle ways that only manifest in a VM. Section 10 widens the lens to architectures beyond amd64, because FreeBSD on arm64 and riscv64 is increasingly common and the virtualisation story there has its own shape. After those two sections, we move into the labs.

## Section 9: Timekeeping, Memory, and Interrupt Handling Under Virtualisation

So far the chapter has concentrated on devices, the visible artefacts that a driver binds to and talks to. But a driver also depends on three ambient services that the kernel provides transparently: time, memory, and interrupts. Under virtualisation, all three change in subtle ways that rarely break a driver outright but often make it behave strangely. A driver that ignores these differences will usually pass functional tests and then fail in production when a user notices that timeouts are wrong, throughput is lower than expected, or interrupts are being lost. This section gathers what every driver author should know.

### Why Time Is Different Inside a VM

On bare metal, the kernel has direct access to several hardware time sources. The TSC (time-stamp counter) reads a per-CPU cycle counter; the HPET (high-precision event timer) provides a system-wide counter; the ACPI PM timer provides a lower-frequency fallback. The kernel chooses one as the current `timecounter(9)` source, wraps it in a small API, and uses it to derive `getbintime`, `getnanotime`, and friends.

Inside a VM, those same sources exist, but they are emulated or passed through, and each has its own quirks. The TSC, which is normally the best source, can become unreliable when the VM migrates between physical hosts, when the host's TSCs are unsynchronised, or when the hypervisor rate-limits the guest in ways that cause TSC skew. The HPET is emulated and costs a VM exit on every read, which is cheap for occasional use but expensive if a driver reads it in a tight loop. The ACPI PM timer is generally reliable but slow.

To address this, major hypervisors expose *paravirtual* clock interfaces. Linux popularised the term `kvm-clock` for the KVM paravirtual clock; Xen has `xen-clock`; VMware has `vmware-clock`; Microsoft's Hyper-V has `hyperv_tsc`. Each of these is a small protocol by which the hypervisor publishes time information in a shared memory page that the guest can read without a VM exit. FreeBSD supports several of them. You can see which the kernel has chosen as follows.

```sh
% sysctl kern.timecounter.choice
kern.timecounter.choice: ACPI-fast(900) i8254(0) TSC-low(1000) dummy(-1000000)

% sysctl kern.timecounter.hardware
kern.timecounter.hardware: TSC-low
```

On a guest under bhyve, the kernel may select `TSC-low` or one of the paravirtual options depending on the CPU flags the hypervisor advertises. The important point is that the choice is automatic and the `timecounter(9)` API is the same regardless.

### What This Means for Drivers

For a driver that only uses the high-level time APIs (`getbintime`, `ticks`, `callout`), nothing needs to change. The `timecounter(9)` abstraction shields you from the underlying details. The driver asks "what time is it" or "how many ticks have elapsed" and the kernel answers correctly, whether on bare metal or in a VM.

Problems arise when a driver bypasses the abstraction and reads time sources directly. A driver that does `rdtsc()` inline and uses the result for timing will be wrong under virtualisation whenever the host's TSC changes (for example, during a live migration). A driver that spins on a device register with a timeout measured in CPU cycles will consume excessive CPU inside a VM where one "CPU cycle" is not a predictable unit.

The cure is simple: use the kernel's time primitives. `DELAY(9)` for short, bounded waits. `pause(9)` for yields that can sleep. `callout(9)` for deferred work. `getbintime(9)` or `getsbinuptime(9)` for clock readings. Each of these is correct under virtualisation because the kernel has already adapted to the environment.

A concrete pattern that breaks in VMs and is surprisingly common is the "reset and wait" sequence.

```c
/* Broken pattern: busy-wait without yielding. */
bus_write_4(sc->res, RESET_REG, RESET_ASSERT);
for (i = 0; i < 1000000; i++) {
	if ((bus_read_4(sc->res, STATUS_REG) & RESET_DONE) != 0)
		break;
}
```

On bare metal, this loop might complete in a few microseconds because the device clears the status bit quickly. In a VM, the bus read and write each cost a VM exit, and the loop body runs much slower because every iteration makes a round trip through the hypervisor. The 1,000,000 iteration bound, which is instant on bare metal, can become a multi-second hang inside a VM. Worse, during a VM pause (a live migration), the guest does not execute at all, and the timeout loses its meaning.

The corrected pattern uses `DELAY(9)` and a bounded real-time wait.

```c
/* Correct pattern: bounded wait with DELAY and a wallclock timeout. */
bus_write_4(sc->res, RESET_REG, RESET_ASSERT);
for (i = 0; i < RESET_TIMEOUT_MS; i++) {
	if ((bus_read_4(sc->res, STATUS_REG) & RESET_DONE) != 0)
		break;
	DELAY(1000);	/* one millisecond */
}
if (i == RESET_TIMEOUT_MS)
	return (ETIMEDOUT);
```

`DELAY(9)` is calibrated against the kernel's time source, so it sleeps for the intended number of microseconds regardless of how fast or slow the CPU is executing at that moment. The loop's bound is now expressed in milliseconds, which is meaningful in both bare-metal and virtualised environments.

### Callouts and Timers Across Migration

A more subtle concern is what happens to a driver's callouts when the VM pauses. If a callout was scheduled to fire in 100 ms and the VM is paused for 5 seconds (during a live migration, for example), does the callout fire 5.1 seconds after it was scheduled, or 100 ms after the VM resumes?

The answer is that it depends on the clock source the `callout(9)` subsystem uses. `callout` uses `sbt` (signed binary time), which under normal circumstances is derived from the selected `timecounter`. For hypervisors that pause the guest's virtual TSC during migration, the callout behaves as if no time had passed during the pause; the 100 ms wait is 100 ms of guest-observed time, which may be 5.1 seconds of wallclock time. For hypervisors that do not pause the virtual TSC, the callout fires at the scheduled wallclock time, which may be "immediately" after the VM resumes.

For most drivers, either behaviour is acceptable. The callout eventually fires and the code it triggers runs. But a driver that measures real-world time (for example, a driver that talks to a hardware device whose state depends on wallclock time) may need to re-sync after a resume. FreeBSD's suspend-and-resume infrastructure provides `DEVMETHOD(device_resume, ...)`, and a driver can detect a resume and take corrective action.

```c
static int
mydrv_resume(device_t dev)
{
	struct mydrv_softc *sc = device_get_softc(dev);

	/*
	 * After a resume (from ACPI suspend or VM pause), the device
	 * may have lost state and the driver's view of time may no
	 * longer align with the device's.  Reinitialise what needs
	 * reinitialising.
	 */
	mydrv_reset(sc);
	mydrv_reprogram_timers(sc);

	return (0);
}
```

`bhyve(8)` supports guest suspend through its own suspend interface (on hypervisors that implement it); the kernel delivers a normal `device_resume` method call on wake-up. A driver that writes `device_resume` correctly works in both cases.

### Memory Pressure, Ballooning, and Pinned Buffers

Drivers that own DMA buffers or pinned memory have a relationship with the memory subsystem that changes under virtualisation. On bare metal, the physical memory the kernel sees is the memory actually installed in the machine. Inside a VM, the guest's "physical" memory is virtual from the host's point of view: it is backed by host RAM plus possibly swap, and its extent can change during the guest's lifetime.

The `virtio-balloon` device is the mechanism hypervisors use to reclaim memory from a guest that is not using it. When the host needs memory, it asks the guest to "inflate" its balloon, which allocates pages from the guest kernel's free pool and declares them unusable. Those pages can then be unmapped from the guest and reused by the host. Conversely, when the host has memory to spare, it can "deflate" the balloon and return pages to the guest.

FreeBSD has a `virtio_balloon` driver (in `/usr/src/sys/dev/virtio/balloon/virtio_balloon.c`) that participates in this protocol. For most drivers, this is invisible: the balloon takes from the general free pool, so only drivers that pin significant amounts of memory can be affected. If your driver allocates a 256 MB DMA buffer and pins it (for a frame buffer, for example), the balloon cannot reclaim that memory. This is the correct behaviour for a pinned buffer, but it does mean that a VM running your driver cannot shrink its memory footprint as much as a VM with only non-pinning drivers.

A pragmatic guideline: avoid allocating more pinned memory than you absolutely need. For buffers that can be allocated on demand, allocate them on demand. For buffers that must be resident, size them to a reasonable working-set bound rather than a pessimistic maximum. The balloon driver will return the rest to the host when it needs to.

### Memory Hotplug

Some hypervisors (including bhyve with appropriate support) can hot-add memory to a running guest. FreeBSD handles this through ACPI events and the generic hotplug machinery. Drivers that cache memory information at attach time must be prepared for that cache to become stale when hotplug occurs; the robust pattern is to re-read the information when needed rather than caching it indefinitely.

Hot-removal of memory is rarer and more delicate. For driver authors, it is usually enough to note that if a driver owns pinned memory, the hypervisor cannot remove it; if the driver owns unpinned memory, the kernel's memory management handles relocation. Drivers that violate this (by expecting that physical addresses they have read from the kernel remain valid forever) will break under memory hot-removal. The fix is to go through `bus_dma(9)` for every physical address that gets programmed into hardware, not to cache physical addresses outside of a DMA map.

### DMA and the IOMMU Path

We covered IOMMU in Section 5 from the host's perspective. Here, from the driver's perspective, there are two practical consequences.

First, every address programmed into hardware must come from a `bus_dma(9)` load operation. Under bare metal without an IOMMU, a driver that programs a physical address obtained from `vmem_alloc` or a similar interface usually works, because physical-equals-bus in that environment. Under passthrough with an IOMMU, the bus address the device needs is not the physical address; it is the IOMMU-mapped address, which `bus_dma_load` computes. A driver that programs physical addresses directly will transfer data to or from the wrong memory, sometimes corrupting unrelated data.

Second, `bus_dma` mappings have lifetimes. A typical pattern is to allocate a DMA tag at attach, allocate a DMA map per buffer, load the buffer, program the device, wait for completion, and then unload the buffer. The load and unload each cost a small amount of CPU, and under the IOMMU they also cost an IOMMU invalidation. For drivers that cycle through many small buffers per second, the invalidation cost can become significant.

The fix, when it applies, is to keep DMA mappings alive for longer. `bus_dma` supports pre-allocated maps that can be reused; a driver that needs to DMA into the same physical region repeatedly can load it once and reuse the bus address until the region is no longer needed. This is a standard optimisation and is entirely orthogonal to virtualisation, but it matters more under passthrough because the IOMMU work is bigger than the host-to-bus work on bare metal.

### Interrupt Delivery in Virtualised Environments

Interrupts have always been the sharp edge of driver design, and under virtualisation they become sharper. The two interrupt styles a driver encounters are *INTx* (pin-based) and *MSI/MSI-X* (message-signalled).

INTx is the old-style interrupt pin. In a real machine, the pin connects through the PCI bus and an interrupt controller (APIC, IOAPIC) to the CPU. In a VM, each INTx delivery requires the hypervisor to intercept the device's pin assertion, map it to an internal interrupt, and inject it into the guest. The intercept and injection both cost VM exits. For low-rate interrupts (the classic "something happened" signal) this is fine. For high-rate interrupts, it can be a bottleneck.

MSI (Message-Signalled Interrupts) and its successor MSI-X avoid the pin entirely. The device writes a small message to a well-known memory-mapped address, and the interrupt controller delivers the corresponding interrupt vector. Under virtualisation, MSI-X works much better than INTx because the hypervisor can map the message write directly to a guest interrupt without needing to intercept every edge transition on a virtual pin. Modern hardware supports *posted interrupts*, which let the hypervisor deliver MSI-X interrupts to a running vCPU without any VM exit at all.

The driver-side implication is clear: prefer MSI-X. FreeBSD's `pci_alloc_msix` API lets a driver request MSI-X interrupts. Most modern drivers already use it. If you are writing a new driver or updating an old one, use MSI-X unless you have a specific reason not to.

```c
static int
mydrv_setup_msix(struct mydrv_softc *sc)
{
	int count = 1;
	int error;

	error = pci_alloc_msix(sc->dev, &count);
	if (error != 0)
		return (error);

	sc->irq_rid = 1;
	sc->irq_res = bus_alloc_resource_any(sc->dev, SYS_RES_IRQ,
	    &sc->irq_rid, RF_SHAREABLE | RF_ACTIVE);
	if (sc->irq_res == NULL) {
		pci_release_msi(sc->dev);
		return (ENXIO);
	}

	error = bus_setup_intr(sc->dev, sc->irq_res,
	    INTR_TYPE_NET | INTR_MPSAFE, NULL, mydrv_intr, sc,
	    &sc->irq_handle);
	if (error != 0) {
		bus_release_resource(sc->dev, SYS_RES_IRQ,
		    sc->irq_rid, sc->irq_res);
		pci_release_msi(sc->dev);
		return (error);
	}

	return (0);
}
```

This is the standard MSI-X setup sequence. The `pci_alloc_msix` call negotiates with the kernel's PCI layer to allocate one MSI-X vector. The resource and interrupt handler are set up as usual. The cleanup path releases the MSI-X vector along with the other resources.

For drivers with multiple queues or multiple event sources, MSI-X supports up to 2048 vectors per device, and a driver can allocate one per queue to avoid lock contention. The `pci_alloc_msix` API supports requesting multiple vectors; `count` is an in-out parameter. Under virtualisation, each vector maps to a separate guest interrupt, and posted interrupts deliver them with no VM exit on modern hardware.

### Interrupt Coalescing and Notification Suppression

Even with MSI-X, an interrupt-per-transaction model can be too expensive under virtualisation. A high-rate device that fires an interrupt for every received packet can generate hundreds of thousands of interrupts per second, and although each is cheap individually, the aggregate cost is noticeable.

Hardware interrupt coalescing addresses this on real devices: the device can be configured to deliver one interrupt for a batch of events rather than one per event. Under VirtIO, the equivalent mechanism is *notification suppression*, exposed through the `VIRTIO_F_RING_EVENT_IDX` feature.

With event indexes, the guest tells the device "do not interrupt me until you have processed up to descriptor N", and the device honours this by checking the guest's used_event field before raising an interrupt. A guest that is already polling the ring does not need an interrupt at all; a guest that wants to be interrupted only after a batch can set the event index to the batch size.

FreeBSD's `virtqueue(9)` framework supports event indexes when negotiated. A driver that knows it may process multiple buffers per interrupt can enable event indexes to reduce interrupt rate. The classic pattern is:

```c
static void
mydrv_intr(void *arg)
{
	struct mydrv_softc *sc = arg;
	void *cookie;
	uint32_t len;

	virtqueue_disable_intr(sc->vq);
	for (;;) {
		while ((cookie = virtqueue_dequeue(sc->vq, &len)) != NULL) {
			/* process the completed buffer */
		}
		if (virtqueue_enable_intr(sc->vq) == 0)
			break;
		/* a new buffer arrived between dequeue and enable; loop again */
	}
}
```

The `virtqueue_disable_intr` call tells the device not to interrupt again until re-enabled. The driver then drains the ring. The `virtqueue_enable_intr` call arms interrupts, but only if no new buffer has arrived in the meantime; if one has, it returns nonzero and the loop continues. This is a standard pattern that minimises interrupt rate without ever missing a completion.

### Putting the Pieces Together

Time, memory, and interrupts are not glamorous topics, but they are where the rubber meets the road for drivers that want to be correct under virtualisation. The guidelines distil to a small set of disciplines:

- Use the kernel's time APIs rather than rolling your own.
- Treat the device's state as a thing that can disappear on resume; write `device_resume` correctly.
- Do not pin more memory than necessary, and go through `bus_dma(9)` for every DMA address.
- Prefer MSI-X over INTx.
- Use the virtqueue event-index mechanism where appropriate to reduce interrupt rate.

These are the disciplines of any well-written driver; under virtualisation they are not optional. A driver that follows them will work in a VM. A driver that violates them will work on bare metal and fail in the field on a customer's VM, sometimes in ways that are difficult to reproduce.

With that grounding in place, we can zoom out one more time and look at how these ideas travel to architectures other than amd64.

## Section 10: Virtualisation on Other Architectures

FreeBSD runs well on amd64, arm64, and riscv64. The virtualisation story on amd64 is the one we have been telling: `bhyve(8)` uses Intel VT-x or AMD-V, guests see PCI-based VirtIO devices, the IOMMU is VT-d or AMD-Vi, and the general flow is familiar. On the other architectures, the pieces are similar but the specifics differ. For a driver author, most of this is invisible, because FreeBSD's virtualisation APIs are architecture-independent. But a few practical points are worth knowing, and a few drivers have architecture-specific behaviour that only surfaces under virtualisation.

### arm64 Virtualisation

On arm64, the hypervisor mode is called *EL2* (Exception Level 2), and the virtualisation extensions are part of the architecture's standard specification (ARMv8-A virtualisation). A guest runs in EL1 under the hypervisor in EL2. There is no direct amd64-style INTx; the interrupt controller is the *GICv3* (Generic Interrupt Controller version 3), and interrupt virtualisation is provided by the GIC's *virtual CPU interface*.

FreeBSD has an arm64 `bhyve(8)` host-side port under development that targets a future release; as of FreeBSD 14.3 the vmm implementation ships only for amd64, so arm64 hosts do not yet run guests natively. The guest side of FreeBSD, however, already uses the same `virtio_mmio` and `virtio_pci` transports that amd64 does, so a FreeBSD guest running on an arm64 hypervisor (for example, KVM or a future arm64 `bhyve`) does not know or care that the host is arm64.

For VirtIO specifically, arm64 guests more often use the MMIO transport. This is a practical consequence of how the virtual platforms are typically configured: arm64 hypervisors often expose VirtIO devices as MMIO regions rather than as emulated PCI buses. FreeBSD's `virtio_mmio.c` (in `/usr/src/sys/dev/virtio/mmio/`) provides the transport. A driver that uses `VIRTIO_DRIVER_MODULE` is automatically compatible with both transports, because the macro registers the driver with both `virtio_mmio` and `virtio_pci`.

This is one of the quiet wins of the `virtio_bus` design. A VirtIO driver written once runs across every VirtIO transport on every architecture that FreeBSD supports. No `#ifdef __amd64__` clauses, no per-architecture translation layer. The `virtio_bus` abstracts the transport; the driver talks to the abstraction.

### riscv64 Virtualisation

On riscv64, virtualisation is provided by the *H extension* (Hypervisor extension). FreeBSD has a `bhyve` port for riscv64 as well, though it is less mature than the amd64 and arm64 ports as of FreeBSD 14.3. The VirtIO transports work the same way: drivers use `VIRTIO_DRIVER_MODULE` and the kernel's `virtio_bus` handles the transport specifics.

For driver authors working on riscv64, the most important thing to know is that the architecture-independent APIs all apply. `bus_dma(9)`, `callout(9)`, `mtx(9)`, `sx(9)`, and the VirtIO framework all work on riscv64. Code that is correct on amd64 usually runs unchanged on riscv64. The differences are in lower-level details (memory ordering, cache flushing, interrupt routing) that the kernel handles under the hood.

### Cross-Architecture Considerations for Driver Authors

If you are writing a driver that should work on multiple architectures, the guidelines from Chapter 29 apply directly. Use the architecture-independent APIs. Avoid inline assembly except where strictly necessary (and then isolate it behind a portable wrapper). Do not assume a specific cache-line size or a specific page size. Do not assume an architecture's endianness unless you have explicitly used a conversion macro.

For virtualisation specifically, the main cross-architecture concern is *which VirtIO transports are present*. On amd64, VirtIO is almost always PCI. On arm64 with QEMU or Ampere hypervisors, VirtIO is often MMIO. On riscv64 with QEMU, VirtIO is often MMIO. A driver that only handles PCI VirtIO will not work in MMIO environments. The fix is to use `VIRTIO_DRIVER_MODULE`, which registers the driver with both transports, and to avoid assuming that the device's parent bus is PCI.

A concrete test: on your target architecture, run `pciconf -l` inside a guest and see whether VirtIO devices appear. If they do, the transport is PCI. If they do not (but the devices work), the transport is MMIO. On arm64 guests, you can also check `sysctl dev.virtio_mmio` to see MMIO VirtIO devices. A driver that works with both transports will not produce different output in these checks, because the `virtio_bus` API is what it interacts with.

### When Architecture Matters for Real

Most drivers are architecture-independent if written in the FreeBSD idiom. The exceptions are:

- Drivers that deal with hardware-specific features: for example, ARM's SMMU is architecturally different from Intel's VT-d, and a driver that manipulates the IOMMU directly (rare) must handle both.
- Drivers that perform byte-order conversion: network drivers do this routinely, but they use portable helpers (`htonl`, `ntohs`, etc.) rather than architecture-specific code.
- Drivers that need specific CPU instructions: for example, cryptographic drivers that use AES-NI on amd64 or the AES extensions on arm64 need per-architecture code paths. These are rare and are already isolated by the kernel's crypto framework.

For most driver work on most architectures, the right mental model is "FreeBSD is FreeBSD". The APIs are the same. The idioms are the same. The virtualisation framework is the same. The architecture is a detail the kernel manages for you.

With architecture considerations covered, we have the complete picture of virtualisation and containerisation as it affects a FreeBSD driver author. The remaining sections of the chapter turn to hands-on practice.

## Hands-on Labs

The labs below walk you through four small exercises that put the chapter's ideas into practice. Each lab has a clear goal, a set of prerequisites, and a series of steps. Companion files are available under `examples/part-07/ch30-virtualisation/` for the ones that need more than a handful of lines of code.

Work through them in order. The first lab gets you comfortable inspecting a VirtIO guest from inside a VM. The second gets you writing a tiny VirtIO-adjacent driver. The third and fourth move into jails and VNET. Allow a couple of hours of hands-on time for the whole set; longer if the `bhyve(8)` or QEMU setup is new to you.

### Lab 1: Exploring a VirtIO Guest

**Goal**: Confirm that you can start a FreeBSD 14.3 guest under `bhyve(8)`, log in, and observe the VirtIO devices it has attached. This establishes the development environment you will use for later labs.

**Prerequisites**: A FreeBSD 14.3 host (bare metal or nested in another hypervisor, provided the outer hypervisor supports nested virtualisation). Enough memory for a small guest (2 GB is plenty). The `vmm.ko` module loadable on the host, which it is on any standard FreeBSD 14.3 kernel.

**Steps**:

1. Fetch a FreeBSD 14.3 VM image. The base project provides prebuilt VM images at `https://download.freebsd.org/`. Choose a `bhyve`-friendly image (typically the "BASIC-CI" or "VM-IMAGE" variants in the `amd64` directory).
2. Install the `vm-bhyve` port or package: `pkg install vm-bhyve`. This wraps `bhyve(8)` in a friendlier management interface.
3. Configure a VM directory: `zfs create -o mountpoint=/vm zroot/vm` (or `mkdir /vm` if you are not using ZFS). In `/etc/rc.conf`, add `vm_enable="YES"` and `vm_dir="zfs:zroot/vm"` (or `vm_dir="/vm"`).
4. Initialise the directory: `vm init`. Copy a default template: `cp /usr/local/share/examples/vm-bhyve/default.conf /vm/.templates/default.conf`.
5. Create a VM: `vm create -t default -s 10G guest0`. Attach the downloaded image: `vm install guest0 /path/to/FreeBSD-14.3-RELEASE-amd64.iso`.
6. Log in once the installer finishes and reboots. The login prompt comes up on `vm console guest0`.
7. Inside the guest, run `pciconf -lvBb | head -40`. You will see virtio-blk, virtio-net, and possibly virtio-random devices, depending on the template. Note the driver each device is bound to.
8. Run `sysctl kern.vm_guest`. The output should be `bhyve`.
9. Run `dmesg | grep -i virtio`. Observe the attach messages for each VirtIO device.
10. Record your observations in a text file. You will refer to them in Labs 2 and 4.

**Expected outcome**: A running VM, a clear listing of attached VirtIO devices, and a confirmed `kern.vm_guest = bhyve` reading.

**Common pitfalls**: Not enabling VT-x or AMD-V in the host's BIOS. Not having enough memory for the guest. Misconfigured network bridge preventing the guest from reaching the network (which is harmless for this lab but will matter later).

### Lab 2: Using Hypervisor Detection in a Kernel Module

**Goal**: Write a small kernel module that reads `vm_guest` and logs the environment at load time. This is the smallest possible example of environment-aware driver behaviour.

**Prerequisites**: A working FreeBSD 14.3 guest from Lab 1. Kernel build tools installed (they come with the base system on the development kit). The `bsd.kmod.mk` build system, accessed via a simple `Makefile`.

**Steps**:

1. Under `/home/ebrandi/FDD-book/examples/part-07/ch30-virtualisation/lab02-detect/` in the companion examples tree, you will find a starter `detectmod.c` and `Makefile`. If you are not using the companion tree, create these files by hand.
2. The source file should define a kernel module that, on load, prints which environment it is in based on `vm_guest`.
3. Build the module: `make clean && make`.
4. Load it: `sudo kldload ./detectmod.ko`.
5. Check the dmesg output: `dmesg | tail`. You should see a line like `detectmod: running on bhyve`.
6. Unload: `sudo kldunload detectmod`.
7. Reboot the guest on a different hypervisor if possible (QEMU/KVM, for example) and re-run. The output should now reflect the new environment.

**Expected outcome**: A module that correctly identifies the hypervisor it is running on, using `vm_guest`.

**Common pitfalls**: Forgetting to include `<sys/systm.h>` for the `vm_guest` declaration. Linking errors from mistyped macros. Forgetting to set `KMOD=` and `SRCS=` in the Makefile.

### Lab 3: A Minimal Character Device Driver Inside a Jail

**Goal**: Write a small character device driver, expose it from the host, create a jail with a custom devfs ruleset that makes the device visible inside, and verify that the jail's processes can use it while the host's privilege checks are still enforced.

**Prerequisites**: A FreeBSD 14.3 host. A working jail setup directory (typically `/jails/`). Basic familiarity with `make_dev(9)`, `d_read`, and `d_write`.

**Steps**:

1. Under `examples/part-07/ch30-virtualisation/lab03-jaildev/`, you will find `jaildev.c` and `Makefile`. The driver creates a `/dev/jaildev` character device whose `read` returns a fixed greeting.
2. Build and load the module on the host: `make && sudo kldload ./jaildev.ko`.
3. Verify `/dev/jaildev` exists and is readable: `cat /dev/jaildev`.
4. Create a jail root: `mkdir -p /jails/test && cp /bin/sh /jails/test/` (this is a minimal jail; for a real setup, use a proper filesystem layout).
5. Add a devfs ruleset to `/etc/devfs.rules`:
   ```text
   [devfsrules_jaildev=100]
   add include $devfsrules_hide_all
   add include $devfsrules_unhide_basic
   add path 'jaildev' unhide
   ```
6. Reload the rules: `sudo service devfs restart`.
7. Start the jail: `sudo jail -c path=/jails/test devfs_ruleset=100 persist command=/bin/sh`.
8. In another terminal, enter the jail: `sudo jexec test /bin/sh`.
9. Inside the jail, run `cat /dev/jaildev`. The greeting should appear. Try `ls /dev/`: only the allowed devices (including `jaildev`) are visible.
10. Test the privilege boundary: modify the driver to require `PRIV_DRIVER` for an ioctl, rebuild, reload, and verify that the jail's root cannot run the ioctl while the host's root can.

**Expected outcome**: A driver visible in the jail only because the ruleset allows it, with privilege checks that behave differently for host root and jail root.

**Common pitfalls**: Forgetting to restart `devfs` after editing the rules. Not setting `persist` on the jail (without it, the jail dies as soon as the initial process exits). Misreading the ruleset syntax (whitespace is significant).

### Lab 4: A Network Driver Inside a VNET Jail

**Goal**: Create a VNET jail, move one end of an `epair(4)` into it, and verify that network traffic flows between the host and the jail using only the jail's VNET. This exercises `if_vmove()` and the VNET lifecycle.

**Prerequisites**: A FreeBSD 14.3 host with `if_epair.ko` loadable. Root privileges.

**Steps**:

1. Load the `if_epair` module: `sudo kldload if_epair`.
2. Create an `epair`: `sudo ifconfig epair create`. You will get a device pair `epair0a` and `epair0b`.
3. Assign an IP to `epair0a` on the host: `sudo ifconfig epair0a 10.100.0.1/24 up`.
4. Create a jail root directory: `mkdir -p /jails/vnet-test`. Place a minimal shell and `ifconfig` binary inside (or bind-mount `/bin`, `/sbin`, `/usr/bin` for testing).
5. Create the jail with VNET enabled:
   ```sh
   sudo jail -c \
       name=vnet-test \
       path=/jails/vnet-test \
       host.hostname=vnet-test \
       vnet \
       vnet.interface=epair0b \
       persist \
       command=/bin/sh
   ```
   The `vnet.interface=epair0b` parameter triggers the `if_vmove()` that moves the interface into the jail.
6. Enter the jail: `sudo jexec vnet-test /bin/sh`.
7. Inside the jail, configure the interface: `ifconfig epair0b 10.100.0.2/24 up`.
8. Still inside the jail, ping the host: `ping -c 3 10.100.0.1`. It should succeed.
9. From the host, ping the jail: `ping -c 3 10.100.0.2`. It should succeed.
10. Stop the jail: `sudo jail -r vnet-test`. The `epair0b` interface is moved back to the host (because it was moved with `vnet.interface`, which uses `if_vmove_loan()` under the hood in recent FreeBSD releases).
11. Verify the interface is back on the host: `ifconfig epair0b`. It should still exist but belong to the host's VNET again.

**Expected outcome**: A jail with its own network stack, moving an interface cleanly in and out.

**Common pitfalls**: Forgetting to enable VNET in the kernel (`options VIMAGE` is enabled in `GENERIC`, so this should be fine, but custom kernels might not have it). Trying to use a physical interface instead of `epair(4)` for the first attempt (this works but causes the host to lose that interface while the jail has it). Not giving the jail enough binaries to run a shell (the simplest workaround is a `nullfs` mount of the host's `/rescue` or a minimal bind-mount setup).

### Lab 5: PCI Passthrough Simulation (Optional)

**Goal**: Observe how PCI passthrough changes a device's ownership, using a non-critical device as the target. This lab is marked optional because it requires a spare PCI device and an IOMMU-capable host. If those are not available, read through the steps; they illustrate the workflow even without running them.

**Prerequisites**: A FreeBSD 14.3 host with VT-d or AMD-Vi enabled in firmware. A spare PCI device that is safe to remove from the host (an unused NIC is a common choice). A `bhyve(8)` guest configuration.

**Steps**:

1. Identify the target device: `pciconf -lvBb | grep -B 1 -A 10 'Ethernet'` (or whatever type the spare device is). Note its bus, slot, and function (e.g., `pci0:5:0:0`).
2. Edit `/boot/loader.conf` and add the device to the passthrough list:
   ```text
   pptdevs="5/0/0"
   ```
3. Reboot the host. The device should now be bound to `ppt(4)` rather than its native driver. Confirm with `pciconf -l` (look for `ppt0`).
4. Configure a guest to pass the device through:
   ```text
   passthru0="5/0/0"
   ```
   (Using the `vm-bhyve` configuration; the raw `bhyve` command is more verbose.)
5. Boot the guest. Inside the guest, run `pciconf -lvBb`. The device now appears in the guest with its real vendor and device IDs, attached to its native driver.
6. Exercise the device inside the guest: configure the NIC, send traffic, verify it works.
7. Shut down the guest. Edit `/boot/loader.conf` to remove the `pptdevs` line, reboot, and verify that the device returns to the host with its native driver attached.

**Expected outcome**: A clean round trip of a PCI device from host to guest and back, exercising the detach and reattach paths of the native driver.

**Common pitfalls**: The host firmware does not have VT-d or AMD-Vi enabled (look for it in the BIOS/UEFI setup). The chosen device is in the same IOMMU group as a device the host needs, forcing a multi-device passthrough. The device is attached to its native driver at boot before `ppt(4)` can claim it (usually this is fine if `pptdevs` is set early enough).

### Lab 6: Building and Loading the vtedu Driver

**Goal**: Build the pedagogical `vtedu` driver from the case study, load it under a FreeBSD 14.3 guest, and observe its behaviour even without a matching backend. This lab exercises the kernel module build process in the VirtIO context and verifies the module structure.

**Prerequisites**: A FreeBSD 14.3 guest (the one from Lab 1 is fine). The FreeBSD source tree under `/usr/src` or the kernel headers installed (`pkg install kernel-14.3-RELEASE` works on stock systems). Root privileges inside the guest.

**Steps**:

1. Copy the companion files to the guest. From the book's examples tree, `examples/part-07/ch30-virtualisation/vtedu/` contains `vtedu.c`, `Makefile`, and `README.md`. Transfer them to the guest (via `scp`, `9p` share, or a shared volume).
2. Inside the guest, change into the `vtedu` directory: `cd /tmp/vtedu`.
3. Build the module: `make clean && make`. If the build fails because `/usr/src` is not installed, install it with `pkg install kernel-14.3-RELEASE` or point `SYSDIR` at an alternative kernel source tree: `make SYSDIR=/path/to/sys`.
4. A successful build produces `virtio_edu.ko` in the current directory.
5. Load the module: `sudo kldload ./virtio_edu.ko`. The load succeeds regardless of whether a matching device is present.
6. Check module status: `kldstat -v | grep -A 5 virtio_edu`. You will see the module is loaded, but no device is bound. This is expected without a backend.
7. Unload the module: `sudo kldunload virtio_edu`.
8. Inspect the module's PNP information: `kldxref -d /boot/modules` lists modules and their PNP entries. If you moved the module into `/boot/modules`, you will see the `VirtIO simple` PNP entry advertising device ID 0xfff0.
9. Look at `dmesg` output during load and unload. There should be no errors. The absence of an attach message confirms that no device is bound, which is the expected behaviour.

**Expected outcome**: A successful build and load cycle that demonstrates the module is well-formed. Without a backend, the module is inert; this is a useful confirmation that the build and load plumbing work independently of the device side.

**Common pitfalls**: Missing kernel sources (install the `src` package). Missing `virtio.ko` dependency (load `virtio` first if it was somehow absent, though it is built into `GENERIC`). Confusion when no device attaches (re-read Section 1 of the vtedu README; this is by design).

**What to do next**: The real learning comes from pairing this module with a backend. Challenge 5 in the Challenge Exercises describes writing a matching backend in `bhyve(8)`. If you complete that, the driver will attach to the backend-provided device, a `/dev/vtedu0` node will appear, and you will be able to `echo hello > /dev/vtedu0 && cat /dev/vtedu0` to exercise the full VirtIO round-trip you studied in the case study.

### Lab 7: Measuring VirtIO Overhead

**Goal**: Quantify the performance characteristics of VirtIO by running a simple workload under emulated, paravirtualised, and passthrough device configurations. This lab is about building intuition for what virtualisation costs, not about optimising a specific driver.

**Prerequisites**: A FreeBSD 14.3 host with bhyve, at least 8 GB of RAM, and the `vm-bhyve` tool installed. A NVMe drive is ideal but not required. The `fio(1)` benchmarking tool (available from ports as `benchmarks/fio`). Optional: a spare NIC for passthrough comparison.

**Steps**:

1. Create a baseline guest with VirtIO block and network devices (this is the default in `vm-bhyve`).
2. Inside the guest, install `fio`: `pkg install fio`.
3. Run a baseline disk benchmark: `fio --name=baseline --rw=randread --bs=4k --size=1G --numjobs=4 --iodepth=32 --runtime=30s --group_reporting`. Record the IOPS and latency.
4. On the host, measure the same workload directly (not inside the guest) using the backing storage as the target. Compare.
5. Run a baseline network benchmark: `iperf3 -c 10.0.0.1 -t 30` (server on the host, client in the guest). Record the throughput.
6. If you have a spare NIC, reconfigure the guest to use passthrough for the NIC (see Lab 5). Re-run the iperf3 benchmark. Compare.
7. On the host, observe interrupt and VM exit counts while the benchmarks run: `vmstat -i`, `pmcstat -S instructions -l 10`. The most interesting counters are VM exit rates, which correlate with overhead.
8. For the disk benchmark, try varying the VirtIO features. Modify the guest configuration to disable `VIRTIO_F_RING_EVENT_IDX` (if the backend supports disabling it) and observe the change in interrupt rate.

**Expected outcome**: A small set of numbers that quantify the cost of virtualisation for your particular setup. Typically, VirtIO paravirtualised I/O is within 10-20% of bare metal for sustained workloads and within 30-40% for latency-sensitive random workloads; passthrough is within a few percent of bare metal but forfeits flexibility; pure emulation (for example, emulated E1000 instead of virtio-net) is 5-10x slower than VirtIO and should be avoided for any serious workload.

**Common pitfalls**: Benchmarks can be noisy; run each multiple times and take medians rather than single samples. The host's cache state affects disk benchmarks; run a warmup pass before the measured runs. CPU frequency scaling can skew results; pin the guest to specific cores and disable scaling on the host for reproducibility.

**What this teaches**: The numbers are secondary; the method is primary. Being able to measure overhead and attribute it to a specific layer (emulation vs. paravirtualisation vs. passthrough, interrupt rate vs. throughput, guest CPU vs. host CPU) is the foundation of performance work in virtualised environments. The same technique applies to any driver you write: if it has a performance requirement, you need to measure, and the measurement techniques are the ones in this lab.

### A Note on Labs You Cannot Complete Today

Not every reader will have the hardware for every lab. Labs 1 through 4 are doable on essentially any FreeBSD 14.3 machine with enough memory and the `vmm` module. Lab 5 requires specific hardware that many readers will not have. Treat Lab 5 as a read-along walkthrough if you cannot run it; the concepts translate to any passthrough scenario, and the exact commands are documented in `pci_passthru(4)` and the `bhyve(8)` manual page.

If you are blocked on a lab, write down exactly what went wrong and come back to it. Virtualisation and containerization are areas where small environmental details can derail a whole setup, and the diagnostic skill of narrowing down the failure is as important as completing the lab.

## Challenge Exercises

The labs above guide you through the standard paths. The challenges below ask you to extend the work. They are harder, less prescriptive, and designed to reward experimentation. Pick whichever one interests you most; all of them stretch different muscles.

### Challenge 1: Extend the Detect Module

The Lab 2 module reads `vm_guest` and prints an environment label. Extend it so that it also reads the CPU vendor string (using `cpu_vendor` and `cpu_vendor_id`), the total physical memory (`realmem`), and the hypervisor signature where applicable. Produce a single structured log line that a test script can parse.

The CPU vendor string is a well-known part of CPU identification; look at `/usr/src/sys/x86/x86/identcpu.c` to see how the kernel reads it. The physical memory total is exposed through the `realmem` global and through the `hw.physmem` sysctl. The hypervisor signature lives in CPUID leaf 0x40000000 on most hypervisors; reading it requires a small piece of assembly or an intrinsic.

The interesting design question is how to expose this information to user space. You could log it at module load, add a sysctl, or create a `/dev/envinfo` device. Each has trade-offs. Think about which is most appropriate for a real production driver.

### Challenge 2: A Simulation Backend for a Real VirtIO Driver

Chapter 29's techniques encourage splitting a driver into accessors, backends, and a thin upper layer. Apply this to `virtio_random`. The real driver (in `/usr/src/sys/dev/virtio/random/virtio_random.c`) is small and tightly written. Can you refactor it so that the virtqueue operations go through an accessor layer, and a simulation backend that does not need a real VirtIO device can be selected for tests?

The refactor is subtle because the VirtIO framework provides most of the accessor layer for you: `virtqueue_enqueue`, `virtqueue_dequeue`, `virtio_notify` are already abstractions. The challenge is to find a layer above them that can be swapped out. One possibility is to move the harvest loop into a function that takes a callback for "get one buffer's worth of data", and implement that callback either as "call the virtqueue" (real) or as "read from `/dev/urandom` on the host" (simulation).

This is a design exercise more than a coding exercise. The goal is to understand how far Chapter 29's advice can be stretched in a real driver that already has good abstractions.

### Challenge 3: A VNET-Aware Driver Skeleton

Write a skeleton kernel module that creates a pseudo-network interface (using the `if_clone(9)` framework), supports being moved between VNETs, and reports its VNET identity in a sysctl. Verify with a VNET jail that the interface can be moved into the jail, used, and moved back.

The key subtlety is handling the VNET context correctly. Read the code in `/usr/src/sys/net/if_tuntap.c` for a reference. The `if_vmove` lifecycle requires the driver to clean up per-VNET state when the interface leaves and re-create it when the interface arrives. Pay attention to `VNET_SYSINIT` and `VNET_SYSUNINIT` if your driver needs per-VNET state.

This is a deep challenge that will take you into the VNET internals. Do not expect to complete it in a single sitting. Treat it as a multi-day project.

### Challenge 4: A Jail-Visible Status Device

Write a driver that exposes a `/dev/status` character device. The device's `read` returns different information depending on whether the caller is in a jail. If the caller is on the host, it returns system-wide status. If the caller is in a jail, it returns jail-specific status (number of processes, current memory use, and so on).

The interesting part is how to distinguish the caller's jail. The `struct thread` passed to the `read` method has `td->td_ucred->cr_prison`, and `prison0` is the host. From the prison pointer you can read the jail's name, its process count (`pr_nprocs`), and so on. Be careful about locking: the prison's fields are mostly read under a mutex that the driver must acquire.

This challenge is a good way to learn about the jail API without writing anything hypervisor-related. It also teaches you about `struct thread` and `struct ucred`, which are central to FreeBSD's privilege model.

### Challenge 5: A bhyve Emulated Device in User Space

If you are ready for a bigger project, study how `bhyve(8)` emulates a simple VirtIO device (for example, virtio-rnd) in user space, and write a new emulated device of your own. The easiest target is a "hello" device that returns a fixed string through a VirtIO interface. The guest-side driver is your Chapter 29 or Chapter 30 work; the host-side emulator lives in `/usr/src/usr.sbin/bhyve/`.

This exercise ties together everything in the chapter: you write a VirtIO device in user space on the host, your driver inside the guest reads from it, the virtqueue transport between them is what you have been studying, and the whole thing exercises the full guest-host loop.

It is a substantial project, and it requires some familiarity with `bhyve(8)`'s code structure. Consider it a reach goal. If you finish it, you have genuinely learned both sides of the VirtIO story.

### Challenge 6: Container Orchestration for Driver Testing

Write a shell script (or a more elaborate tool) that automates the Lab 3 and Lab 4 workflows. Given a driver and a test harness, the script should:

1. Build the driver.
2. Load it on the host.
3. Create a jail (or a VNET jail) with an appropriate ruleset.
4. Run the harness inside the jail.
5. Collect results.
6. Destroy the jail.
7. Unload the driver.
8. Report pass/fail with diagnostics.

The script should be idempotent (running it twice should not leave residue) and it should handle common failures gracefully. The end result is a tool you can run in CI to verify that your driver continues to work under jails as it evolves.

This is not a deep technical challenge but a practical one. Building this kind of automation is a significant fraction of real-world driver work, and building it yourself once teaches you what is involved.

### How to Approach These Challenges

Each challenge could be a weekend project. Pick one that seems interesting and set aside time for it. Do not try to do all of them at once; your energy for unfamiliar work is limited, and you learn more from one challenge finished than from three challenges half-done.

If you get stuck, do two things. First, re-read the relevant section of the chapter; the hints are there. Second, look at real FreeBSD drivers that do something similar. The source tree is your best teacher for the idiomatic way to solve problems that have been solved before.

## Troubleshooting and Common Mistakes

This section collects the problems that driver authors most often hit when working with virtualised and containerised environments. Each entry describes the symptom, the likely cause, and the way to verify and fix it. Keep this as a reference; the first time you see a symptom, it will be new, and the next time it will be familiar.

### VirtIO Device Not Detected in the Guest

**Symptom**: The guest boots, but `pciconf -l` does not show the VirtIO device, and `dmesg` contains no `virtio` attach messages.

**Cause**: Either the host did not configure the device (wrong `bhyve` command line, wrong `vm-bhyve` template), or the guest kernel is missing the VirtIO module. On FreeBSD 14.3, VirtIO drivers are built into `GENERIC` and auto-load; the guest side should almost never be the cause.

**Fix**: On the host, verify that the `bhyve` command line includes the device. For `vm-bhyve`, look at the VM's configuration file and check for lines like `disk0_type="virtio-blk"` or `network0_type="virtio-net"`. If the device is listed but still missing, check that the hypervisor has permission to access the backing resource (the disk image, the tap device).

Inside the guest, confirm the kernel has VirtIO: `kldstat -m virtio` or `kldstat | grep -i virtio`. If the module is not loaded, try `kldload virtio`.

### virtqueue_enqueue Fails With ENOBUFS

**Symptom**: A VirtIO driver tries to enqueue a buffer and gets `ENOBUFS`.

**Cause**: The virtqueue is full. Either the driver has not been draining completed buffers through `virtqueue_dequeue`, or the ring size is smaller than expected and the driver is enqueueing more than it can hold.

**Fix**: Call `virtqueue_dequeue` in the interrupt handler to drain completed buffers and free their associated state. If the ring is genuinely too small, negotiate a larger ring size at feature-negotiation time, if the device supports `VIRTIO_F_RING_INDIRECT_DESC` or similar features.

A common beginner error is to forget that enqueue produces a descriptor that must be matched by a dequeue. Every successful enqueue must eventually produce a dequeue; otherwise the ring fills up.

### Passthrough Device Not Available in the Guest

**Symptom**: The host has marked a device as passthrough-capable, the guest is configured to use it, but the guest does not see the device.

**Cause**: Several possible. The host did not actually bind the device to `ppt(4)` at boot (check `pciconf -l` for `pptN`). The host's firmware does not have the IOMMU enabled (check `dmesg | grep -i dmar` or `grep -i iommu`). The device is in an IOMMU group that cannot be split (so you need to pass through the whole group).

**Fix**: Verify each of the above in turn. `dmesg | grep -i dmar` should show initialisation messages from `dmar(4)` on Intel hosts or `amdvi(4)` on AMD hosts. If those are missing, enable VT-d or AMD-Vi in the firmware. If the device is in a shared IOMMU group, either pass through the whole group or move the device to a different PCI slot that is better isolated (an administrator task requiring chassis access).

### Kernel Panic at Module Load Inside a Guest

**Symptom**: `kldload` of a new driver inside a guest causes a kernel panic.

**Cause**: Usually a bug in the driver, but sometimes a VirtIO-specific one: the driver assumes a device feature that the backend does not provide, or it accesses config space using the wrong layout (legacy versus modern).

**Fix**: Use the guest as your development platform precisely to make panics cheap. Capture the panic output (either from the serial console or from a `bhyve` log), narrow down the faulting function with `ddb(4)` if you have it configured, and iterate. The combination of a VM (so panics are cheap) and `printf` debugging (so you can see what happens before the panic) is usually enough to fix a VirtIO driver bug quickly.

For VirtIO-specific bugs, double-check the feature bits your driver negotiates. A driver that claims a feature but then does not implement it correctly (for example, claims `VIRTIO_F_VERSION_1` but uses legacy config space) will break in surprising ways.

### Jail Cannot See a Device Even After Unhiding

**Symptom**: A devfs ruleset includes the device, the ruleset is applied to the jail, but the jail's `ls /dev` does not show it.

**Cause**: The `devfs` mount inside the jail was not re-mounted or re-ruled after the ruleset change. devfs caches the visibility decision at mount time, so later changes do not propagate until the mount is refreshed.

**Fix**: Run `devfs -m /jails/myjail/dev rule -s 100 applyset` (replace the path and ruleset number as appropriate) to force a reapply. Alternatively, stop the jail, restart `devfs`, and start the jail again. The `service devfs restart` command applies the rules in `/etc/devfs.rules` to the host's `/dev`; for jails, you generally need to restart the jail.

### Privilege Denied Inside a Jail That Should Work

**Symptom**: A driver operation works on the host as root but fails with `EPERM` inside a jail's root.

**Cause**: The operation requires a privilege that `prison_priv_check` denies by default. This is the intended behaviour. The fix is either to use a less-privileged operation, to configure the jail with `allow.*` to grant the privilege, or (if appropriate) to change the driver to use a different privilege.

**Fix**: Look at the driver's `priv_check` call and identify which privilege is being checked. Consult `/usr/src/sys/sys/priv.h` and `prison_priv_check` in `/usr/src/sys/kern/kern_jail.c` to see whether the privilege is allowed inside jails. If it is not, consider whether the restriction is appropriate (usually yes) and adjust the jail's configuration accordingly (`allow.raw_sockets`, `allow.mount`, etc.).

Do not be tempted to remove the `priv_check` call just to make the jail work. The check is there for a reason; work with the framework, not against it.

### VNET Jail Cannot Send or Receive Packets

**Symptom**: A VNET jail is created, an interface is moved in, but network traffic does not flow.

**Cause**: Several possibilities. The interface was not configured inside the jail (each VNET has its own ifconfig state). The default route was not set inside the jail. The host's firewall is blocking traffic between the jail's interface and the rest of the network. The interface is not in an `UP` state.

**Fix**: Inside the jail, run `ifconfig` and confirm the interface has an address and is `UP`. Run `netstat -rn` and confirm the routing table has the entries you expect. If you are using `pf(4)` or `ipfw(8)` on the host, check the rules: filter rules apply to the host's VNET, and the jail's packets may be rejected if the host's filter blocks them (depending on how the topology is set up).

For `epair(4)` setups specifically, remember that both ends need configuration, and that the host side stays on the host. The jail configures the end that was moved in; the host configures the end that stayed.

### PCI Passthrough Driver Fails to Attach in the Guest

**Symptom**: The guest sees the passed-through device in `pciconf -l`, but the driver fails to attach, or the attach succeeds but `read`/`write` fails.

**Cause**: Usually one of two things. Either the driver assumes a platform-specific feature that is not present in the guest (an ACPI table, a BIOS entry), or the driver is programming physical addresses directly rather than going through `bus_dma(9)`, and the IOMMU is redirecting DMA in a way the driver does not expect.

**Fix**: For the first case, look at the driver's attach code for calls that read platform tables (ACPI, FDT, etc.). If the guest's firmware does not expose the expected tables, the driver must either provide defaults or refuse gracefully.

For the second case, audit the driver's DMA code. Every address programmed into the device must come from a `bus_dma(9)` load operation, not from a `vtophys` call or similar. This is standard practice anyway, but it becomes mandatory under passthrough.

### Host Becomes Unresponsive When a Guest Starts

**Symptom**: Running `bhyve(8)` makes the host sluggish or unresponsive.

**Cause**: Resource contention. The guest is allocated more memory or more VCPUs than the host can afford to share. The host is swapping or spinning on a shared lock.

**Fix**: Check the guest's resource allocation. A guest with all the host's memory will starve the host; a guest with more VCPUs than the host has cores will cause thrashing. A common rule of thumb is to give guests no more than half the host's memory and no more VCPUs than (host cores - 1), to leave room for the host itself.

If the sluggishness persists after sensible allocation, check `top -H` on the host for the `bhyve(8)` process and its threads. Heavy CPU use by `bhyve(8)` suggests the guest is doing something CPU-intensive; heavy CPU use by `vmm` kernel threads suggests excessive VM exits, which may indicate a guest driver that is polling too aggressively.

### `kldunload` Hangs

**Symptom**: Unloading a driver module hangs the process and cannot be interrupted.

**Cause**: Some resource the driver owns is still in use. A file descriptor is still open on the driver's device, a callout is still scheduled, a taskqueue still has pending tasks, or a kernel thread the driver spawned has not exited.

**Fix**: Find and release the holder. `fstat` or `lsof` lists open file descriptors; `procstat -kk` shows kernel threads. The driver's module unload handler must drain every async mechanism it starts: cancel callouts, drain taskqueues, wait for kernel threads to exit, close any held file descriptors. If any of these is missing, unload hangs.

For VNET-aware drivers, the unload must correctly clean up per-VNET state. A common mistake is to clean up in `mod_event(MOD_UNLOAD)` but forget that one of the VNETs the module is attached to is not the current one; accessing its state without `CURVNET_SET` leads to either a wrong-context access (fast) or a hang (slow). The correct pattern is to iterate over VNETs and clean up each explicitly.

### Timing-Related Bugs Only in VMs

**Symptom**: The driver works on bare metal but hangs or loses interrupts inside a VM.

**Cause**: Timing assumptions that fail under virtualisation. Guest execution is sometimes paused for milliseconds at a time (during VM exits), and a driver that polls a status register in a tight loop without yielding may fail to make progress or may consume excessive CPU.

**Fix**: Replace tight polling with `DELAY(9)` for microsecond-scale waits, `pause(9)` for short waits, or proper sleep with `tsleep(9)` for longer waits. Use interrupt-driven designs instead of polling wherever possible. Test with virtio-blk's performance counters to see whether the driver is generating an unreasonable number of VM exits.

A driver that works correctly on bare metal but fails on a VM is almost always making a timing assumption. The solution is to use the kernel's time primitives correctly; they work on both.

### VirtIO Negotiation Fails or Returns Unexpected Features

**Symptom**: The driver logs that feature negotiation produced a feature set that is missing bits you expected, or the probe-and-attach path succeeds but the device behaves unexpectedly.

**Cause**: Two classes of issue. The first is that the device (or its backend) advertises a feature set that does not include the bit you requested. This is normal when the hypervisor's backend is older or intentionally minimal. The second is that the guest-side code is requesting a feature bit that the framework does not know about, in which case the framework may strip it silently.

**Fix**: Log `sc->features` immediately after `virtio_negotiate_features` returns. Compare it to the set you requested. If the device is missing a bit you thought was mandatory, your driver needs to fall back gracefully or refuse to attach with a clear error message. Never assume a bit is present without checking the post-negotiation value.

For backend-side investigation (if you are using a hypervisor whose source you can read, such as `bhyve(8)` or QEMU), look at the device emulator's feature advertisement. The backend holds the truth: the guest sees only what the backend advertises. A mismatch between what you expect and what you see almost always traces back to the backend.

### `bus_alloc_resource` Fails Inside a Guest

**Symptom**: The attach path calls `bus_alloc_resource_any` or `bus_alloc_resource` and receives `NULL`, causing the driver to fail attach.

**Cause**: Under a hypervisor, the device's resources (BARs, IRQ lines, MMIO windows) may differ from their bare-metal layout. A driver that hard-codes resource IDs or assumes specific BAR numbers can fail if the hypervisor presents a different layout.

**Fix**: Always use `pci_read_config(dev, PCIR_...)` to read actual BAR contents rather than assuming. Use `bus_alloc_resource_any` with the rid obtained from the resource list, not a hard-coded number. If the resource allocation still fails, compare `pciconf -lvBb` output from bare metal and from the guest to see what has changed.

A concrete example: a device that uses BAR 0 for MMIO and BAR 2 for I/O on bare metal may be configured differently by the hypervisor. Always read the BARs at runtime and allocate resources based on what is actually present.

### `kldload` Succeeds but No Device Attaches

**Symptom**: `kldload` of a VirtIO driver returns success, but `kldstat -v` shows no device bound to the module, and no dmesg messages are produced.

**Cause**: The driver's PNP table does not match any device advertised by the hypervisor. This is normal when a backend is not providing the expected device. For `vtedu` in the case study, this is the expected behaviour without a matching `bhyve(8)` backend.

**Fix**: Run `devctl list` (or `devinfo -v`) on the host or in the guest to see which devices are present but unbound. If the device is not listed at all, the backend is not running or is misconfigured. If the device is listed but unbound, check its PNP identifiers (`vendor`, `device` for PCI, or the VirtIO type ID for VirtIO) and compare against the driver's PNP table. Mismatch is the most common cause.

A common beginner error is to believe that `kldload` success means the driver is working. It only means the module loaded. Use `kldstat -v | grep yourdriver` to see whether any device has been claimed.

### Module Is Loaded but `/dev` Node Does Not Appear

**Symptom**: The driver has loaded, `kldstat -v` shows it attached to a device, but the expected `/dev` node does not appear.

**Cause**: Either the driver did not call `make_dev(9)`, or the `devfs` mount inside the current jail is filtered by a ruleset that hides the node, or the `make_dev` call failed silently because the device unit number clashed with an existing one.

**Fix**: On the host, check `ls /dev/yourdev*`. If it is missing there too, the driver did not create the node. Check the attach path for the `make_dev` call, and verify its return value. If the node is present on the host but missing inside a jail, the devfs ruleset is the cause. Run `devfs -m /path/to/jail/dev rule show` to see the active ruleset inside the jail.

For a driver that is intended to be visible inside jails, the correct practice is to document which devfs ruleset exposes the node and to provide an example ruleset in the driver's README. Do not assume the administrator will figure it out.

### Virtqueue Interrupts Never Fire

**Symptom**: The driver's interrupt handler is never called, even though the driver has submitted work to the virtqueue.

**Cause**: One of several possibilities. The backend never processes the work (bug in the backend). The driver did not register the interrupt handler correctly. The driver did not call `virtio_setup_intr`, so no interrupt plumbing exists. The driver disabled interrupts via `virtqueue_disable_intr` and never re-enabled them.

**Fix**: Systematically check each step. In the attach path, verify that `virtio_setup_intr` was called and returned 0. In the interrupt handler, verify that you are re-enabling interrupts when appropriate. Add a `printf` at the top of the handler to confirm it is never called. If the handler is genuinely never called, run `vmstat -i | grep yourdriver` to see the interrupt count; a count of zero confirms the interrupt is not arriving.

If the count is nonzero but the handler does no work, the handler is running but finding nothing in the virtqueue. This suggests the backend is acknowledging but not producing real completions; look at the backend.

### Interrupt Storm Under Virtualisation

**Symptom**: Inside a VM, `vmstat -i` shows interrupt rates in the hundreds of thousands per second, and CPU utilisation is high even without real work.

**Cause**: An interrupt that is not being cleared, or a driver that interrupts on every event without coalescing. Under INTx specifically, a level-triggered interrupt that remains asserted causes the handler to be called in a loop.

**Fix**: For MSI-X, confirm that the handler acknowledges completions and that the virtqueue ring is being drained. For INTx, confirm that the handler clears the device's interrupt status register. For VirtIO specifically, negotiate `VIRTIO_F_RING_EVENT_IDX` if the backend supports it; this lets the device suppress unnecessary interrupts.

Look at the Section 9 pattern with `virtqueue_disable_intr` / `virtqueue_enable_intr`. A correct driver disables interrupts on entry, drains the ring, and only re-enables interrupts when the ring is empty. Missing this structure is a common cause of interrupt storms.

### DMA Failures Only Under Passthrough

**Symptom**: The driver works correctly with an emulated device, but when the same hardware is passed through via `ppt(4)`, DMA transfers fail silently or corrupt memory.

**Cause**: Most often, the driver is programming physical addresses directly rather than going through `bus_dma(9)`. Under emulation, the hypervisor intercepts all I/O and translates addresses on the fly, hiding the bug. Under passthrough, the device DMA's directly through the IOMMU, and the physical address the driver programmed is not the bus address the IOMMU expects.

**Fix**: Audit every place the driver computes an address to program into the device. Each must come from `bus_dma_load` or `bus_dma_load_mbuf` or similar, not from `vtophys` or a raw physical address. This is a mandatory discipline for passthrough and is strongly recommended for all drivers.

A useful diagnostic: enable IOMMU verbose logging (`sysctl hw.dmar.debug=1` on Intel, or the equivalent on AMD) and watch the kernel log for IOMMU page faults while the driver runs. A page fault reveals exactly which bus address the device attempted to access; if it does not match a mapped region, the driver's address calculation is wrong.

### A VirtIO Device Appears but Has the Wrong Type

**Symptom**: The guest sees a VirtIO device at the expected PCI address, but `pciconf -lv` or `devinfo` reports a different VirtIO device type than expected.

**Cause**: Device-ID confusion. The PCI vendor ID for VirtIO is always 0x1af4, but the device ID encodes the VirtIO type, and different VirtIO versions use different device-ID ranges. Legacy VirtIO uses 0x1000 + VIRTIO_ID. Modern VirtIO uses 0x1040 + VIRTIO_ID. A hypervisor that exposes a mix of modern and legacy devices can confuse a driver that only probes one range.

**Fix**: FreeBSD's `virtio_pci` transport handles both ranges transparently, so most drivers are immune. For driver authors who inspect `pciconf -lv` directly, be aware that both 0x1000-0x103f (legacy) and 0x1040-0x107f (modern) are VirtIO. The `VIRTIO_DRIVER_MODULE` macro registers the driver with both transports and does the right thing for both device-ID ranges.

### Jail-Specific Ioctl Fails With ENOTTY

**Symptom**: An ioctl that works on the host returns `ENOTTY` when issued from inside a jail, even though the ioctl number is recognised by the driver.

**Cause**: The driver's ioctl handler checks jail visibility and returns `ENOTTY` to hide the ioctl's existence from jailed callers. This is a security-by-obscurity pattern used by some drivers that expose host-only administrative operations through otherwise jail-visible devices.

**Fix**: If the jail should be able to use the ioctl, review the driver's visibility check. The idiomatic approach is to return `EPERM` (permission denied) rather than `ENOTTY` when an operation exists but is not permitted; `ENOTTY` implies the ioctl does not exist, which can confuse callers. Consider whether the jailed caller should see the ioctl; if so, remove the hiding logic and use `priv_check` for access control instead.

### VNET Move Leaks Per-VNET State

**Symptom**: After moving an interface in and out of a VNET multiple times, the kernel's memory usage grows, eventually triggering a memory pressure event.

**Cause**: The driver allocates per-VNET state when an interface enters a VNET but does not free it when the interface leaves. Each VNET move leaks a fixed amount of memory.

**Fix**: Implement the VNET-move lifecycle correctly. When an interface enters a VNET (`if_vmove` into the new VNET), allocate per-VNET state. When it leaves (`if_vmove` out), free it. The `CURVNET_SET` and `CURVNET_RESTORE` pair delineates the VNET context; use them when allocating or freeing.

Look at `/usr/src/sys/net/if_tuntap.c` for a correct VNET-move implementation. The lifecycle is subtle and easy to get wrong; a reference implementation is the best teacher.

### Guest Kernel Panics with "Fatal Trap 12" on First I/O

**Symptom**: The guest boots, the driver attaches, the first user-space I/O to the driver's device causes a "Fatal Trap 12: page fault while in kernel mode" panic.

**Cause**: Almost always a NULL pointer dereference in the driver's `read`, `write`, or `ioctl` path. Under virtualisation, the fault is immediate rather than merely corrupt-then-continue, because the guest's memory protection is exact.

**Fix**: Use the kernel's debugger (`ddb(4)` or `dtrace(1)`) to find the faulting instruction. A typical cause is a `dev->si_drv1 = sc` that was forgotten in attach, so when user space opens `/dev/yourdriver` and calls `read`, `dev->si_drv1` is NULL. The fix is to always set `si_drv1` in attach, right after creating the cdev.

Under VMs, these panics are cheap: fix the code, rebuild, reload. On bare metal, each panic costs a reboot. One more reason to develop under virtualisation.

### Live Migration Fails or Causes Guest Hang

**Symptom**: A guest under a hypervisor that supports live migration (currently limited in `bhyve(8)`, more common in Linux KVM and VMware) is migrated to a different host, and the guest hangs or corrupts after migration.

**Cause**: The guest driver holds state that is tied to the source host (a specific physical TSC, a specific IOMMU mapping, a specific passed-through device). Migration transfers the guest's memory and CPU state but cannot transfer physical hardware state.

**Fix**: For driver authors, the advice is simple: do not cache values that are tied to the physical host. Re-read TSC frequency from `timecounter(9)` rather than storing a local copy. Do not pass through PCI devices that you need to migrate. For VirtIO devices, live migration is supported by the standard, and the guest driver needs no special code.

If you are writing a driver for a live-migration-supporting environment, the main design rule is: all state should be in the guest's memory; anything on the host side should be recreatable after migration. Standard VirtIO drivers meet this bar because the virtqueue state is in guest memory.

### Unexpected devfs Entries Appear Inside a Jail

**Symptom**: A jail has a minimal devfs ruleset, but entries appear that the administrator did not expect.

**Cause**: Either the ruleset was not applied correctly, or the jail inherited the default ruleset before the custom one was applied, or a new device appeared after the ruleset was set.

**Fix**: Run `devfs -m /jail/path/dev rule show` to see what rules are active. Compare with `/etc/devfs.rules`. If a later rule is adding visibility the earlier rule denied, the order is wrong. If the jail was started before the ruleset was finalised, restart the jail.

A robust practice is to always start a jail with a known ruleset specified in `/etc/jail.conf` rather than relying on the devfs default. The `devfs_ruleset = NNN` directive in the jail configuration ensures the jail's devfs mount uses the expected ruleset from the moment the jail starts.

### Two Drivers Fight Over the Same Device

**Symptom**: Loading driver A works, but loading driver B after A (or vice versa) causes one of them to fail attach with a cryptic message about resource conflicts.

**Cause**: Two drivers are trying to claim the same device. This can happen if the device has multiple valid drivers (for example, a generic driver and a specific driver for a particular chipset variant) and the load order determines which one wins.

**Fix**: FreeBSD's Newbus arbitrates driver priority through `DRIVER_MODULE`'s ordering, but the exact semantics depend on which driver attached first. The general rule is that once a device is attached, another driver cannot steal it. If you need to switch drivers, detach the first one (`devctl detach yourdev0`) before loading the second.

Under virtualisation this can show up when you load a test driver after the VirtIO framework has already attached a production driver to the device. The test driver must either use a different PNP entry or explicitly detach the existing one.

### `vm_guest` Shows "no" Inside an Obvious VM

**Symptom**: `sysctl kern.vm_guest` returns "no" inside a guest that is definitely running under a hypervisor.

**Cause**: The hypervisor is not setting the CPUID hypervisor-present bit, or is setting a vendor string that the FreeBSD kernel does not recognise. This can happen with exotic or customised hypervisors.

**Fix**: This is informational, not a fix. If your driver needs to detect virtualisation but `vm_guest` does not cooperate, use alternative signals: the presence of VirtIO devices, the absence of physical hardware that would be present on bare metal, specific CPUID leaves that expose hypervisor-specific information. But recognise that accurate hypervisor detection is hard to guarantee; design your driver to work correctly regardless of the environment, and use `vm_guest` only for non-critical defaults.

### General Diagnostic Approach

When something breaks in a virtualised or containerised environment, the pattern for diagnosis is consistent. First, reduce the environment to the simplest setup that shows the problem: strip the VM down to a single VirtIO device, remove extra jails, disable VNET if it is not needed. Second, try the same operation at the next layer up: if the problem shows in a jail, try it on the host; if on the host, try it on a guest; if in a guest, try it on bare metal. The layer where the problem disappears is usually the layer where the problem lives.

Third, once you have localised the layer, add logging. `printf` in the kernel is still a valid debugging tool; combined with `dmesg`, it gives you a timestamped trace of what the driver does. `dtrace(1)` is more capable but has a higher setup cost. For a first diagnosis, `printf` is usually enough.

Fourth, if the problem is a race or a timing issue, simplify before you complicate. A race that happens once in ten thousand iterations can be investigated with a test loop that runs ten thousand iterations. A timing issue under VM execution can be made reproducible by pinning the VM to specific host CPUs and disabling frequency scaling.

None of this is specific to virtualisation. It is good general debugging discipline. Virtualisation just happens to be an environment where the discipline pays off quickly, because the layers are clearly separated and the layers can be swapped in and out easily.

## Wrapping Up

Virtualisation and containerization are two names for what they are in FreeBSD: two different answers to the same broad question. Virtualisation multiplies kernels; containerization subdivides one. A driver lives inside a kernel, and the way it interacts with its environment depends on which of these is in use.

The virtualisation side is where the VirtIO story lives. VirtIO is the mature, standardised, high-performance paravirtual interface between a guest driver and a hypervisor-provided device. FreeBSD's VirtIO framework (in `/usr/src/sys/dev/virtio/`) implements the standard cleanly, and the `virtio_random` driver you studied in Section 3 is a minimal example of how a VirtIO driver is structured. The key concepts, feature negotiation, virtqueue management, notification, are the same whether the device is a random-number generator, a network card, or a block device. Learn them once, and every VirtIO driver becomes more approachable.

The hypervisor detection mechanism, `vm_guest`, gives a driver a small window into the environment. It is useful for adjusting defaults but dangerous when used to work around bugs. The right mindset is "this is informational"; the wrong mindset is "this is a branch target".

The host side, where FreeBSD runs `bhyve(8)` and provides devices to guests, is where driver authorship meets hypervisor authorship. Most driver authors never touch `vmm(4)` directly, but the PCI passthrough facility is worth understanding, because it exercises the detach and reattach paths that a well-written driver already supports. A driver that survives a round trip through `ppt(4)` is a driver whose lifecycle is honest.

The containerization side is where jails, devfs rulesets, and VNET come together. Jails share the kernel, so drivers are not multiplied; they are filtered. The filter operates on three axes: which devices the jail can see (devfs rulesets), which privileges it can exercise (`prison_priv_check` and `allow.*`), and how much it can consume (`rctl(8)` and `racct(9)`). For a driver author, the design decisions are small and local: pick sensible `devfs` names, call `priv_check` correctly, handle allocation failures gracefully.

The VNET framework extends the jail model into the network stack. It is the part of jails that comes closest to requiring explicit driver cooperation. Drivers that write to per-VNET state from outside a network-stack entry point (for example, from callouts) must establish the VNET context with `CURVNET_SET`. Drivers that survive `if_vmove` cleanly are drivers that will work in VNET jails.

Together, these mechanisms shape FreeBSD as a platform for both hypervisor workloads and container workloads. A single kernel, a single driver set, several different ways for that driver to appear to its users. Understanding the mechanisms is what lets you write drivers that work correctly across all of them, without special-casing any of them.

The discipline the chapter has advocated, sometimes explicitly, sometimes by example, is a continuation of Chapter 29's theme. Write clean abstractions. Use the framework APIs. Do not reach around them. Do not invent your own DMA or privilege or visibility mechanisms; FreeBSD already has well-tested ones. If you do all of this, your driver will work in environments you did not have in mind when you wrote it, and that is the best definition of portable a driver can have.

If you worked through the labs, you now have hands-on experience with starting a `bhyve` guest, writing a small module that uses `vm_guest`, putting a character device behind a devfs ruleset in a jail, and moving a network interface through VNET. Those four skills are all you need to start doing real work on driver code that runs in virtualised and containerised environments. Everything else in the chapter is supporting context.

If you tackled one or more of the challenges, you have stretched further: into simulation backends, into VNET-aware interfaces, into the bhyve emulator itself, or into test automation. Any of these is a genuine piece of FreeBSD driver craft, and the work compounds. Every hour you spend on these topics comes back in every driver you write afterwards.

A final note on attitude. Virtualisation and containerization can feel overwhelming because they introduce so many new pieces at once: hypervisors, paravirtual devices, jails, VNET, rulesets, privileges. But every piece has a clear purpose and a small, well-designed API. The sense of overwhelm goes away once you have seen each one in isolation, and this chapter's pacing was chosen to let you do that. If you are still feeling overwhelmed, go back to Section 3 (VirtIO) or Section 6 (jails and devfs) and re-read with a specific small question in mind. The answers are there.

## Looking Ahead: Security and Privilege

Chapter 31, "Security and Privilege in Device Drivers," builds directly on the foundations laid in this chapter. Jails and virtual machines are one kind of security boundary; drivers have many others. A driver that exposes an `ioctl` is a driver that has created a new interface into the kernel, and that interface must be checked, validated, and restricted.

Chapter 31 will cover the privilege framework in depth (you saw `priv_check` here; Chapter 31 goes through the whole list), the `ucred(9)` structure and how credentials flow through the kernel, the `capsicum(4)` capability framework for finer-grained restrictions, and the MAC (Mandatory Access Control) framework for policy-based security. It will also revisit jails from a security-first perspective, complementing the container-first perspective of this chapter.

The thread that runs through Chapters 29, 30, and 31 is environment. Chapter 29 was about architectural environment: running on the same kernel with different buses or bit-widths. Chapter 30 was about operational environment: running inside a VM, a container, or on a host. Chapter 31 is about policy environment: running under the constraints a security-conscious administrator chooses to apply. A driver that handles all three kinds of environment well is a driver that can be deployed anywhere FreeBSD runs.

With Chapter 31, Part 7 approaches its midpoint. The remaining chapters of the part turn to debugging (Chapter 32), testing (Chapter 33), performance tuning (Chapter 34), and specialised driver topics in later chapters. Each builds on what you have learned so far. Take the time to let this chapter settle; the concepts will keep paying off as you continue.

## Case Study: Designing a Pedagogical VirtIO Driver

This closing case study pulls together the threads of the chapter into a single design walkthrough. It does not present a complete implementation. Instead, it walks through the decisions you would make if you sat down today to design a pedagogical VirtIO driver called `vtedu`. The driver does nothing useful in production, but it exercises enough of the VirtIO surface to be a valuable teaching tool for future readers.

### What vtedu Is For

Imagine that `vtedu` is meant to serve as the example driver for a future FreeBSD workshop on paravirtualised devices. Its job is to expose a single virtqueue, accept write requests from user space, pass them through the virtqueue to a backend in `bhyve(8)` (which does something simple like echoing bytes back), and deliver the echoed bytes back to user space. It must be small enough to read in an afternoon and complete enough to demonstrate the full VirtIO lifecycle.

The design choices below explain each step. A reader who finishes this section should be able to reason about any similar VirtIO driver in the same terms.

### Choosing the Device Identifier

VirtIO defines a set of well-known device IDs in `/usr/src/sys/dev/virtio/virtio_ids.h`. For a pedagogical driver, a reserved or experimental ID is appropriate. The VirtIO specification reserves some ranges for "vendor-specific" devices, and a workshop driver would pick one of those.

For `vtedu`, we pick a hypothetical `VIRTIO_ID_EDU = 0xfff0` (chosen so it does not collide with real device IDs). The corresponding backend in `bhyve(8)` would register the same ID. A real project would coordinate with the `bhyve(8)` maintainers on the ID assignment.

### Defining the Features

A teaching driver should negotiate a meaningful feature bit so that the reader sees feature negotiation in action. `vtedu` defines one feature:

```c
#define VTEDU_F_UPPERCASE	(1ULL << 0)
```

When negotiated, the backend returns the input bytes uppercased. When not negotiated, it returns them unchanged. The driver advertises the feature, the backend may or may not support it, and the negotiation produces whichever outcome both sides can support.

This kind of trivial feature is a teaching device. In a real driver, features correspond to real capabilities; in `vtedu`, the feature exists just to show how negotiation works.

### Softc Layout

The softc is the per-instance state:

```c
struct vtedu_softc {
	device_t		dev;
	struct virtqueue	*vq;
	uint64_t		features;
	struct mtx		lock;
	struct cdev		*cdev;
	struct sglist		*sg;
	char			buf[VTEDU_BUF_SIZE];
	size_t			buf_len;
};
```

The device handle, the virtqueue pointer, the negotiated feature mask, a mutex protecting the driver's serialised access, a `cdev` for the user-space interface, a pre-allocated scatter-gather list, a buffer for the data, and its current length.

### Transport Registration

The module uses `VIRTIO_DRIVER_MODULE` as always:

```c
static device_method_t vtedu_methods[] = {
	DEVMETHOD(device_probe,		vtedu_probe),
	DEVMETHOD(device_attach,	vtedu_attach),
	DEVMETHOD(device_detach,	vtedu_detach),
	DEVMETHOD_END
};

static driver_t vtedu_driver = {
	"vtedu",
	vtedu_methods,
	sizeof(struct vtedu_softc)
};

VIRTIO_DRIVER_MODULE(vtedu, vtedu_driver, vtedu_modevent, NULL);
MODULE_VERSION(vtedu, 1);
MODULE_DEPEND(vtedu, virtio, 1, 1, 1);
```

The PNP info for `vtedu` advertises `VIRTIO_ID_EDU`, so the framework binds the driver to any VirtIO device of that type, under either PCI or MMIO transport.

### Probe and Attach

The probe is a one-liner using `VIRTIO_SIMPLE_PROBE`. The attach sets up the device in the standard order:

```c
static int
vtedu_attach(device_t dev)
{
	struct vtedu_softc *sc = device_get_softc(dev);
	int error;

	sc->dev = dev;
	mtx_init(&sc->lock, device_get_nameunit(dev), NULL, MTX_DEF);

	virtio_set_feature_desc(dev, vtedu_feature_descs);

	error = vtedu_negotiate_features(sc);
	if (error != 0)
		goto fail;

	error = vtedu_alloc_virtqueue(sc);
	if (error != 0)
		goto fail;

	error = virtio_setup_intr(dev, INTR_TYPE_MISC);
	if (error != 0)
		goto fail;

	sc->sg = sglist_alloc(2, M_WAITOK);
	sc->cdev = make_dev(&vtedu_cdevsw, device_get_unit(dev),
	    UID_ROOT, GID_WHEEL, 0600, "vtedu%d", device_get_unit(dev));
	if (sc->cdev == NULL) {
		error = ENXIO;
		goto fail;
	}
	sc->cdev->si_drv1 = sc;

	device_printf(dev, "attached (features=0x%lx)\n",
	    (unsigned long)sc->features);
	return (0);

fail:
	vtedu_detach(dev);
	return (error);
}
```

This follows the standard rhythm: negotiate, allocate queue, setup interrupts, set up user-space interface. It is structurally identical to `virtio_random`'s attach, with a `cdev` creation added because `vtedu` exposes a character device.

### Feature Negotiation

Negotiation is straightforward:

```c
static int
vtedu_negotiate_features(struct vtedu_softc *sc)
{
	uint64_t features = VIRTIO_F_VERSION_1 | VTEDU_F_UPPERCASE;

	sc->features = virtio_negotiate_features(sc->dev, features);
	return (virtio_finalize_features(sc->dev));
}
```

The driver advertises the two features, gets back an intersection, and finalises. The intersection tells the driver whether `VTEDU_F_UPPERCASE` is in play. Subsequent code uses `virtio_with_feature(sc->dev, VTEDU_F_UPPERCASE)` to check.

### Queue Allocation

A single virtqueue is allocated with an interrupt callback:

```c
static int
vtedu_alloc_virtqueue(struct vtedu_softc *sc)
{
	struct vq_alloc_info vq_info;

	VQ_ALLOC_INFO_INIT(&vq_info, 0, vtedu_vq_intr, sc, &sc->vq,
	    "%s request", device_get_nameunit(sc->dev));

	return (virtio_alloc_virtqueues(sc->dev, 0, 1, &vq_info));
}
```

The maximum indirect-descriptor size is zero (no indirects), the interrupt callback is `vtedu_vq_intr`, and the virtqueue pointer is stored in `sc->vq`. A single queue is enough for a request-response pattern where the same queue is used for both directions.

### The Character Device Interface

User space opens `/dev/vtedu0` and writes bytes to it. The driver accepts them, issues a VirtIO request, waits for the response, and exposes the response back to user space through read.

```c
static int
vtedu_write(struct cdev *dev, struct uio *uio, int flags __unused)
{
	struct vtedu_softc *sc = dev->si_drv1;
	size_t n;
	int error;

	n = uio->uio_resid;
	if (n == 0 || n > VTEDU_BUF_SIZE)
		return (EINVAL);

	mtx_lock(&sc->lock);
	error = uiomove(sc->buf, n, uio);
	if (error == 0) {
		sc->buf_len = n;
		error = vtedu_submit(sc);
	}
	mtx_unlock(&sc->lock);
	return (error);
}
```

The write copies bytes into the softc buffer, sets `buf_len`, and calls `vtedu_submit`, which does the VirtIO enqueue and notify.

```c
static int
vtedu_submit(struct vtedu_softc *sc)
{
	int error;

	sglist_reset(sc->sg);
	error = sglist_append(sc->sg, sc->buf, sc->buf_len);
	if (error != 0)
		return (error);

	error = virtqueue_enqueue(sc->vq, sc, sc->sg, 1, 1);
	if (error != 0)
		return (error);

	virtqueue_notify(sc->vq);
	return (0);
}
```

One readable segment (the driver's write into the buffer) and one writable segment (the device's write of the result). The cookie is `sc` itself; the interrupt handler will receive it back when the completion arrives.

### The Interrupt Handler

Completions are processed in a taskqueue-deferred or immediate fashion. For pedagogical simplicity, `vtedu` processes them immediately in the interrupt callback:

```c
static void
vtedu_vq_intr(void *arg)
{
	struct vtedu_softc *sc = arg;
	void *cookie;
	uint32_t len;

	mtx_lock(&sc->lock);
	while ((cookie = virtqueue_dequeue(sc->vq, &len)) != NULL) {
		/* cookie == sc; len is the length the device wrote. */
		sc->buf_len = len;
		wakeup(sc);
	}
	mtx_unlock(&sc->lock);
}
```

The handler drains all completions, updates the softc, and wakes any sleeper waiting on the buffer. For a real driver, this would be richer; for `vtedu`, it is all we need.

### The Read Path

User space then reads the result:

```c
static int
vtedu_read(struct cdev *dev, struct uio *uio, int flags __unused)
{
	struct vtedu_softc *sc = dev->si_drv1;
	int error;

	mtx_lock(&sc->lock);
	while (sc->buf_len == 0) {
		error = mtx_sleep(sc, &sc->lock, PCATCH, "vteduR", 0);
		if (error != 0) {
			mtx_unlock(&sc->lock);
			return (error);
		}
	}
	error = uiomove(sc->buf, sc->buf_len, uio);
	sc->buf_len = 0;
	mtx_unlock(&sc->lock);
	return (error);
}
```

If no result is available, the read sleeps on `sc`, which the interrupt handler will wake. This is the standard "block until ready" pattern for character devices with slow underlying I/O.

### Detach

The detach reverses the attach, cleanly:

```c
static int
vtedu_detach(device_t dev)
{
	struct vtedu_softc *sc = device_get_softc(dev);

	if (sc->cdev != NULL)
		destroy_dev(sc->cdev);
	if (sc->sg != NULL)
		sglist_free(sc->sg);
	virtio_stop(dev);
	if (mtx_initialized(&sc->lock))
		mtx_destroy(&sc->lock);
	return (0);
}
```

`virtio_stop` resets the device status so it no longer generates interrupts. The `cdev` is destroyed, the scatter-gather list freed, and the mutex destroyed.

### Putting vtedu Together

This walkthrough has touched on every major element of a VirtIO driver:

1. Device ID selection and PNP info.
2. Feature definition and negotiation.
3. Softc layout and locking.
4. Virtqueue allocation with interrupt callback.
5. User-space interface through `cdev`.
6. Request submission through enqueue + notify.
7. Completion handling through the interrupt callback.
8. Clean detach.

The whole driver, fully implemented, fits in around 300 lines of C. That is less than `virtio_random` once you add in the backend plumbing. Because the user-space interface is richer than `virtio_random`'s (which hides in the `random(4)` framework), the code is slightly larger in total, but the VirtIO-specific part is no bigger.

### Using vtedu for Teaching

A `vtedu` driver would be used in a workshop something like this:

1. The instructor starts by demonstrating the driver loading, attaching, and processing a write-then-read round trip.
2. Students follow along, typing the key sections from a cheat sheet.
3. The instructor introduces feature negotiation by showing what happens when `VTEDU_F_UPPERCASE` is negotiated (outputs arrive uppercased) versus when it is not.
4. Students modify the driver to add a second feature: "reverse the bytes". They learn how features compose.
5. Finally, the instructor shows how to run the driver in a jail (it just works, as long as `vtedu0` is in the devfs ruleset), illustrating the containerisation side of the chapter.

This is a pedagogical design, not a production one. Its purpose is to show how the pieces fit. A reader who has followed this case study should be able to write a similar driver of their own, starting from scratch or starting from `virtio_random.c` as a template.

### What vtedu Does Not Do

For honesty: `vtedu` as sketched here omits several things a production driver would include. It does not support multiple in-flight requests (the lock serialises everything). It does not handle queue-full situations gracefully (it assumes one request at a time). It does not support module-wide module-event cleanup (just per-device). It does not demonstrate indirect descriptors (because the feature is irrelevant for 256-byte messages).

Each of these is an exercise the reader could undertake after understanding the base design. The chapter's challenge exercises hint at some of them; a dedicated workshop would develop them further.

### A Teaching Driver Versus a Real Driver

Before leaving `vtedu`, a note about the difference between a teaching driver and a real one. A teaching driver is designed for readability. Its code makes every concept visible, often at the cost of clever optimisations. A real driver is designed for reliability and performance. It compresses the common-case paths, adds error handling for every edge case, and optimises for the actual workload.

The temptation when moving from learning to production is to start with the teaching driver and add features. That usually produces worse code than starting with an architectural sketch and filling it in. A teaching driver is a reference; a real driver is a system. The two are not on the same continuum.

As a driver author, your job is to understand the teaching driver well enough that the real-driver design decisions become clear. This chapter's treatment of VirtIO is meant to get you to that understanding. The next step is yours.

## Appendix: Quick Reference

The reference tables below condense the chapter's key facts into a form
you can return to during driver work. They are not a substitute for the
explanatory text; think of them as the one-page cheat sheet for the day
you are actually writing code and need a specific detail.

### VirtIO Core API Functions

| Function | Purpose |
|----------|---------|
| `virtio_negotiate_features(dev, mask)` | Advertise and negotiate feature bits. |
| `virtio_finalize_features(dev)` | Seal the feature negotiation. |
| `virtio_with_feature(dev, feature)` | Test whether a feature is negotiated. |
| `virtio_alloc_virtqueues(dev, flags, nvqs, info)` | Allocate a set of virtqueues. |
| `virtio_setup_intr(dev, type)` | Install the negotiated interrupt handlers. |
| `virtio_read_device_config(dev, offset, dst, size)` | Read device-specific configuration. |
| `virtio_write_device_config(dev, offset, src, size)` | Write device-specific configuration. |

### virtqueue(9) Functions

| Function | Purpose |
|----------|---------|
| `virtqueue_enqueue(vq, cookie, sg, readable, writable)` | Push a scatter-gather chain onto the available ring. |
| `virtqueue_dequeue(vq, &len)` | Pop one completed chain from the used ring. |
| `virtqueue_notify(vq)` | Tell the host new work is available. |
| `virtqueue_poll(vq, &len)` | Wait for a completion and return it. |
| `virtqueue_empty(vq)` | Check whether the queue has any pending work. |
| `virtqueue_full(vq)` | Check whether the queue has space for another enqueue. |

### vm_guest Values

| Constant | String via `kern.vm_guest` | Meaning |
|----------|---------------------------|---------|
| `VM_GUEST_NO` | `none` | Bare metal |
| `VM_GUEST_VM` | `generic` | Unknown hypervisor |
| `VM_GUEST_XEN` | `xen` | Xen |
| `VM_GUEST_HV` | `hv` | Microsoft Hyper-V |
| `VM_GUEST_VMWARE` | `vmware` | VMware ESXi / Workstation |
| `VM_GUEST_KVM` | `kvm` | Linux KVM |
| `VM_GUEST_BHYVE` | `bhyve` | FreeBSD bhyve |
| `VM_GUEST_VBOX` | `vbox` | Oracle VirtualBox |
| `VM_GUEST_PARALLELS` | `parallels` | Parallels |
| `VM_GUEST_NVMM` | `nvmm` | NetBSD NVMM |

### Default devfs Rulesets

| Number | Name | Purpose |
|--------|------|---------|
| 1 | `devfsrules_hide_all` | Start with everything hidden. |
| 2 | `devfsrules_unhide_basic` | Essential devices (`null`, `zero`, `random`, etc.). |
| 3 | `devfsrules_unhide_login` | Login-related devices (`pts`, `ttyv*`). |
| 4 | `devfsrules_jail` | Standard non-VNET jail ruleset. |
| 5 | `devfsrules_jail_vnet` | Standard VNET jail ruleset. |

### Common Privilege Constants

| Constant | Typical jail policy |
|----------|--------------------|
| `PRIV_DRIVER` | Denied (driver-private ioctls). |
| `PRIV_IO` | Denied (raw I/O port access). |
| `PRIV_KMEM_WRITE` | Denied (kernel memory writes). |
| `PRIV_KLD_LOAD` | Denied (module loading). |
| `PRIV_NET_SETLLADDR` | Denied (MAC address changes). |
| `PRIV_NETINET_RAW` | Denied unless `allow.raw_sockets`. |
| `PRIV_NET_BPF` | Allowed via `allow.raw_sockets` for BPF-needing tools. |

### VNET Macros

| Macro | Purpose |
|-------|---------|
| `VNET_DEFINE(type, name)` | Declare a per-VNET variable. |
| `VNET(name)` | Access the per-VNET variable for the current VNET. |
| `V_name` | Conventional shorthand for `VNET(name)`. |
| `CURVNET_SET(vnet)` | Establish a VNET context on the current thread. |
| `CURVNET_RESTORE()` | Tear down the context. |
| `VNET_SYSINIT(name, ...)` | Register a per-VNET init function. |
| `VNET_SYSUNINIT(name, ...)` | Register a per-VNET uninit function. |

### bhyve and Passthrough Tools

| Tool | Purpose |
|------|---------|
| `bhyve(8)` | Run a virtual machine. |
| `bhyvectl(8)` | Query and control running VMs. |
| `vm(8)` | High-level management (via `vm-bhyve` port). |
| `pciconf(8)` | Show PCI devices and their driver bindings. |
| `devctl(8)` | Explicit control of driver attach/detach. |
| `pptdevs` in `/boot/loader.conf` | Bind devices to the passthrough placeholder. |

### Manual Pages Worth Bookmarking

- `virtio(4)` - VirtIO framework overview.
- `vtnet(4)`, `virtio_blk(4)` - Specific VirtIO drivers.
- `bhyve(8)`, `bhyvectl(8)`, `vmm(4)` - Hypervisor user and kernel interfaces.
- `pci_passthru(4)` - PCI passthrough mechanism.
- `jail(8)`, `jail.conf(5)`, `jls(8)`, `jexec(8)` - Jail management.
- `devfs(5)`, `devfs(8)`, `devfs.rules(5)` - devfs and rulesets.
- `if_epair(4)`, `vlan(4)`, `if_tap(4)` - Pseudo-interfaces useful for jails.
- `rctl(8)`, `racct(9)` - Resource control.
- `priv(9)` - Privilege framework.

### Top Five Things a Driver Author Should Do

If you remember nothing else from this chapter, remember these five habits:

1. Use `bus_dma(9)` for every DMA buffer. Never pass physical addresses
   directly to the hardware. This is the single most important habit for
   passthrough and IOMMU-protected environments.
2. Use `priv_check(9)` for privileged operations. Do not hardcode
   `cred->cr_uid == 0` checks. The framework extends your code to jails
   for free.
3. Keep device node names predictable. Administrators who write devfs
   rulesets need to know what to unhide. Document the name in your driver's
   manual page.
4. Handle detach cleanly. Release every resource, cancel every callout,
   drain every taskqueue, and never assume the softc will be re-used.
   Passthrough and VNET both exercise detach heavily.
5. Establish VNET context around callout and taskqueue code that touches
   per-VNET state. `CURVNET_SET` / `CURVNET_RESTORE` is the boilerplate,
   and missing it is the most common VNET-related bug.

These five habits between them cover nearly all of the "works under
virtualisation and containerization" work a driver author needs to do.
Everything else is refinement.

## Appendix: Common Code Patterns

A small catalogue of patterns that appear repeatedly in FreeBSD drivers
that run in virtualised or containerised environments. Each one is a
snippet you can adapt to your own code.

### Pattern: Environment-aware default

```c
static int
mydev_default_interrupt_moderation(void)
{

	switch (vm_guest) {
	case VM_GUEST_NO:
		return (1);	/* tight moderation on bare metal */
	default:
		return (4);	/* loose moderation under a hypervisor */
	}
}
```

Use this pattern to seed a default that the user can override via sysctl.
Do not branch on the specific hypervisor brand unless there is a real
reason.

### Pattern: Privilege-gated ioctl

```c
case MYDEV_IOC_DANGEROUS:
	error = priv_check(td, PRIV_DRIVER);
	if (error != 0)
		return (error);
	/* perform the dangerous operation */
	return (0);
```

The default position for driver-specific privileges is to require
`PRIV_DRIVER`. Consult `priv(9)` if a more specific privilege fits
better.

### Pattern: VNET-aware callout

```c
static void
mydev_callout(void *arg)
{
	struct mydev_softc *sc = arg;

	CURVNET_SET(sc->ifp->if_vnet);
	/* read or write V_ variables, or call network-stack functions */
	CURVNET_RESTORE();

	callout_reset(&sc->co, hz, mydev_callout, sc);
}
```

Any callout, taskqueue function, or kernel thread that might access VNET
state must establish the context. Missing this is the most common source
of VNET-related bugs.

### Pattern: VirtIO attach skeleton

```c
static int
mydev_attach(device_t dev)
{
	struct mydev_softc *sc = device_get_softc(dev);
	int error;

	sc->dev = dev;
	virtio_set_feature_desc(dev, mydev_feature_descs);

	error = mydev_negotiate_features(sc);
	if (error != 0)
		goto fail;

	error = mydev_alloc_virtqueues(sc);
	if (error != 0)
		goto fail;

	error = virtio_setup_intr(dev, INTR_TYPE_MISC);
	if (error != 0)
		goto fail;

	/* post initial buffers, register with subsystem, etc. */
	return (0);

fail:
	mydev_detach(dev);
	return (error);
}
```

The "negotiate, allocate, setup interrupts, start" rhythm is standard for
every VirtIO driver. Every VirtIO attach in `/usr/src/sys/dev/virtio/` is
a variation on this skeleton.

### Pattern: Clean detach

```c
static int
mydev_detach(device_t dev)
{
	struct mydev_softc *sc = device_get_softc(dev);

	/* Stop accepting new work. */
	sc->detaching = true;

	/* Drain async mechanisms. */
	if (sc->co_initialised)
		callout_drain(&sc->co);
	if (sc->tq != NULL)
		taskqueue_drain_all(sc->tq);

	/* Release hardware resources. */
	if (sc->irq_cookie != NULL)
		bus_teardown_intr(dev, sc->irq_res, sc->irq_cookie);
	if (sc->irq_res != NULL)
		bus_release_resource(dev, SYS_RES_IRQ, 0, sc->irq_res);
	if (sc->mem_res != NULL)
		bus_release_resource(dev, SYS_RES_MEMORY, 0, sc->mem_res);

	/* Destroy devfs nodes. */
	if (sc->cdev != NULL)
		destroy_dev(sc->cdev);

	return (0);
}
```

Detach must be symmetric with attach: every resource attach allocated must
be released here, in reverse order. A clean detach is what makes the
driver safe for passthrough and for unload.

### Pattern: make_dev with sensible defaults

```c
sc->cdev = make_dev(&mydev_cdevsw, 0, UID_ROOT, GID_WHEEL, 0600, "mydev%d",
    device_get_unit(dev));
if (sc->cdev == NULL)
	return (ENXIO);
sc->cdev->si_drv1 = sc;
```

Use `0600` mode for nodes that should only be root-accessible, `0644` for
read-any / write-root, `0666` for rare read-and-write-any cases. The
`si_drv1` field is the conventional back-pointer from a `struct cdev` to
the driver's softc.

## Appendix: Glossary

A short glossary of the terms this chapter relies on. Refer back when a
term in a later section has slipped from memory.

- **Bare metal**: A system running directly on physical hardware with no
  hypervisor in between.
- **bhyve**: FreeBSD's native type-2 hypervisor. Runs as a user-space
  program backed by the kernel's `vmm(4)` module.
- **Container**: In FreeBSD, a jail or a user-space framework built on top
  of jails.
- **Credential (`struct ucred`)**: The per-process security context that
  carries uid, gid, jail pointer, and privilege-related state.
- **devfs**: The special filesystem at `/dev` where devices are exposed.
- **devfs ruleset**: A named set of rules that controls which devfs nodes
  are visible in a given devfs mount.
- **Emulated device**: A hypervisor-provided device that imitates a
  real-hardware interface.
- **Guest**: The operating system running inside a virtual machine.
- **Host**: The physical machine (or container-enclosing system) that
  hosts guests or jails.
- **Hypervisor**: Software that creates and manages virtual machines.
- **IOMMU**: A unit between a device and host memory that remaps DMA
  addresses. Enables safe passthrough.
- **Jail**: FreeBSD's lightweight containerisation mechanism.
- **Paravirtual device**: A device with an interface designed to be easy
  for a hypervisor to emulate and for a guest to drive. VirtIO is the
  canonical example.
- **Passthrough**: Giving a guest direct access to a physical device.
- **Prison**: The kernel's internal name for a jail. `struct prison` is
  the data structure.
- **rctl / racct**: The resource-control and resource-accounting
  frameworks that enforce per-jail or per-process limits.
- **Ruleset**: See *devfs ruleset*.
- **Transport (VirtIO)**: The bus-level mechanism that carries VirtIO
  messages. Examples: PCI, MMIO.
- **Virtqueue**: The shared-ring data structure at the heart of VirtIO.
- **VirtIO**: The paravirtualisation standard used by most modern
  hypervisors.
- **VM**: Virtual machine.
- **VNET**: FreeBSD's virtual network stack framework, providing per-jail
  independent stacks.
- **vmm**: FreeBSD's kernel hypervisor core module, used by `bhyve(8)`.

## Appendix: Observing VirtIO with DTrace

DTrace is one of FreeBSD's most capable diagnostic tools, and it is especially useful for understanding how a VirtIO driver behaves at runtime. This appendix collects several concrete DTrace recipes for VirtIO observability. None of them require modifying the driver; they work against the unmodified kernel because the `fbt` (function-boundary-tracing) provider instruments every kernel function.

### A First Probe: Counting virtqueue Enqueues

The simplest useful probe counts how often `virtqueue_enqueue` is called per second.

```sh
sudo dtrace -n 'fbt::virtqueue_enqueue:entry /pid == 0/ { @[probefunc] = count(); } tick-1sec { printa(@); trunc(@); }'
```

Running this on a busy VM shows numbers like:

```text
virtqueue_enqueue 12340
virtqueue_enqueue 15220
virtqueue_enqueue 11890
```

Each number is enqueues per second across all virtqueues. A number in the thousands is normal for an active VM; a number in the millions suggests a pathological driver. The per-VM baseline depends on workload, but getting familiar with the baseline for your environment lets you spot anomalies later.

### Separating Queues by Virtqueue

To see which virtqueue is being used, extend the probe to key on the virtqueue name.

```sh
sudo dtrace -n '
fbt::virtqueue_enqueue:entry
{
	this->vq = (struct virtqueue *)arg0;
	@[stringof(this->vq->vq_name)] = count();
}
tick-1sec
{
	printa(@);
	trunc(@);
}
'
```

Output now looks like:

```text
vtnet0-rx           482
vtnet0-tx           430
virtio_blk0         8220
```

This instantly reveals which device is doing work. A disk-heavy workload shows `virtio_blk0` dominating; a network-heavy workload shows `vtnet0-tx` and `vtnet0-rx`. This kind of first-level breakdown is usually enough to localise a performance problem.

Note that the example dereferences an internal struct (`struct virtqueue`). The struct's layout is implementation-detail and can change between FreeBSD releases. Check `/usr/src/sys/dev/virtio/virtqueue.c` if the struct layout has changed and the probe fails.

### Measuring Time Spent in Enqueue

Time-in-function is a standard DTrace recipe.

```sh
sudo dtrace -n '
fbt::virtqueue_enqueue:entry
{
	self->ts = vtimestamp;
}
fbt::virtqueue_enqueue:return
/ self->ts /
{
	@["virtqueue_enqueue"] = quantize(vtimestamp - self->ts);
	self->ts = 0;
}
'
```

The output is a histogram of how long each `virtqueue_enqueue` call took, in nanoseconds. For healthy VirtIO, most calls complete in the low microsecond range. A significant tail suggests lock contention (the function's mutex is held for too long), memory pressure, or expensive scatter-gather computation.

### Watching for VM Exits

VM exits are the fundamental cost of virtualisation. DTrace's `vmm` probes (if available) let you count them.

```sh
sudo dtrace -n 'fbt:vmm::*:entry { @[probefunc] = count(); } tick-1sec { printa(@); trunc(@); }'
```

On a busy host, this shows dozens of `vmm` functions. The ones to watch are `vm_exit_*`, which handle different exit types (I/O, interrupt, hypercall). Seeing `vm_exit_inout` in the top results suggests the guest is doing a lot of I/O through emulated devices and would benefit from VirtIO.

### Tracing a Specific Driver

To focus on a specific driver's functions, narrow the probe to its module name.

```sh
sudo dtrace -n 'fbt:virtio_blk::*:entry { @[probefunc] = count(); } tick-1sec { printa(@); trunc(@); }'
```

This counts all `virtio_blk` function entries. On a quiescent VM, the output is empty; on an active VM, you see every function the driver calls, with counts. This is useful for getting a feel for a driver's internal structure.

### Tracing a Custom Driver

For the `vtedu` driver from the case study, the same technique works as long as the module is loaded and named `virtio_edu`.

```sh
sudo dtrace -n 'fbt:virtio_edu::*:entry { @[probefunc] = count(); } tick-1sec { printa(@); trunc(@); }'
```

If no backend is attached, the counts will be zero. If a backend is attached and the driver is exercising the virtqueue, you see `vtedu_write`, `vtedu_submit_locked`, `vtedu_vq_intr`, and `vtedu_read` in the counts, in rough proportion to usage.

### Observing the Guest-Host Boundary

A more ambitious use of DTrace is to correlate guest-side events with host-side events. This is possible when both sides are FreeBSD and DTrace is available on both. Run DTrace on the host to count `vmm` exits and on the guest to count driver calls; the numbers should correlate one-for-one on an unloaded system, diverging as the host batches exits or as posted interrupts kick in.

This is an advanced technique and mostly interesting for performance analysis. For day-to-day debugging, single-side DTrace is usually enough.

### Saving and Reusing Probes

The `dtrace(1)` command-line form is fine for quick investigations. For repeated use, save probes to a file and invoke with `dtrace -s`.

```sh
% cat > virtio_probes.d <<'EOF'
#pragma D option quiet

fbt::virtqueue_enqueue:entry
{
	this->vq = (struct virtqueue *)arg0;
	@[stringof(this->vq->vq_name)] = count();
}

tick-1sec
{
	printa(@);
	trunc(@);
}
EOF

% sudo dtrace -s virtio_probes.d
```

A curated set of probes in `.d` files is a good investment for anyone who spends significant time debugging VirtIO.

### When DTrace Cannot Help

DTrace can observe the kernel and, with `pid` provider, most user-space programs. It cannot directly observe guest kernels from the host, because the guest is a process whose internal structure `dtrace` does not know. You can trace the `bhyve(8)` process itself (as a user-space program) using the `pid` provider, which shows what `bhyve` is doing but not what its guest is doing.

For guest-side tracing, DTrace runs inside the guest. If the guest is FreeBSD 14.3, the full DTrace toolkit is available. If the guest is Linux, use `bpftrace` or `perf` instead; they are different tools with similar capabilities.

### A Closing Word

DTrace is one of FreeBSD's competitive advantages. Every driver author should be comfortable with it; the investment pays off repeatedly across years of debugging. This appendix gives you a starting point; the FreeBSD Handbook's DTrace chapter and the original *DTrace: Dynamic Tracing in Oracle Solaris* book (freely available online) are the next steps if you want to go deeper.

## Appendix: A Complete bhyve Configuration Walkthrough

Readers who want to run the labs need a working `bhyve(8)` setup. This appendix walks through a complete configuration, from a bare FreeBSD 14.3 host to a guest with VirtIO devices, in enough detail for a beginner to reproduce it. If you have already built `bhyve` environments, skim this; the goal is to provide a reference for readers who have not.

### The Host Side

Start with a FreeBSD 14.3 host. Confirm the virtualisation extensions are enabled.

```sh
% sysctl hw.vmm
hw.vmm.topology.cores_per_package: 0
hw.vmm.topology.threads_per_core: 0
hw.vmm.topology.sockets: 0
hw.vmm.topology.cpus: 0
...
```

If `hw.vmm` is absent entirely, the `vmm(4)` module is not loaded. Load it with `kldload vmm`. Add `vmm_load="YES"` to `/boot/loader.conf` to load it at every boot.

If `vmm` is loaded but the VT-x/AMD-V features are absent, enable them in the host firmware. The setting is usually called "VT-x", "VMX", "AMD-V", or "SVM" in the BIOS/UEFI menu. After enabling, reboot.

### Install vm-bhyve

`vm-bhyve` is a wrapper that makes `bhyve(8)` easier to use. Install it from ports or packages.

```sh
% sudo pkg install vm-bhyve
```

Create the VM directory. Using ZFS is convenient because it supports snapshotting.

```sh
% sudo zfs create -o mountpoint=/vm zroot/vm
```

Or plain UFS:

```sh
% sudo mkdir /vm
```

Enable `vm-bhyve` and set its directory in `/etc/rc.conf`.

```text
vm_enable="YES"
vm_dir="zfs:zroot/vm"
```

For UFS, use `vm_dir="/vm"` instead. Initialise the directory.

```sh
% sudo vm init
% sudo cp /usr/local/share/examples/vm-bhyve/config_samples/default.conf /vm/.templates/
```

Edit `/vm/.templates/default.conf` if you want different defaults (memory size, CPU count).

### Create and Install a Guest

Download a FreeBSD 14.3 installation image. The file `FreeBSD-14.3-RELEASE-amd64-disc1.iso` is the standard installer; for faster setup, use `FreeBSD-14.3-RELEASE-amd64.qcow2` if you prefer a prebuilt image.

```sh
% sudo vm iso https://download.freebsd.org/releases/amd64/amd64/ISO-IMAGES/14.3/FreeBSD-14.3-RELEASE-amd64-disc1.iso
```

Create the guest.

```sh
% sudo vm create -t default -s 20G guest0
```

Start the installer.

```sh
% sudo vm install guest0 FreeBSD-14.3-RELEASE-amd64-disc1.iso
```

Connect to the console.

```sh
% sudo vm console guest0
```

The FreeBSD installer runs; follow it through to completion. At the end, reboot the guest.

### Configure Networking

`vm-bhyve` supports two network styles: bridged and NAT. For simplicity, bridged is fine.

Create a bridge.

```sh
% sudo vm switch create public
% sudo vm switch add public em0
```

Replace `em0` with your host's physical interface. Assign guests to the switch in their configuration.

```sh
% sudo vm configure guest0
```

In the editor that opens, ensure `network0_switch="public"` is set.

### Start the Guest

```sh
% sudo vm start guest0
% sudo vm console guest0
```

Log in, run `pciconf -lvBb`, and confirm the VirtIO devices are present.

```sh
# pciconf -lvBb
hostb0@pci0:0:0:0:      class=0x060000 rev=0x00 ...
virtio_pci0@pci0:0:2:0: class=0x010000 rev=0x00 vendor=0x1af4 device=0x1001 ...
    vendor     = 'Red Hat, Inc.'
    device     = 'Virtio 1.0 block device'
virtio_pci1@pci0:0:3:0: class=0x020000 rev=0x00 vendor=0x1af4 device=0x1041 ...
    vendor     = 'Red Hat, Inc.'
    device     = 'Virtio 1.0 network device'
...
```

### Troubleshooting the Host Side

If the guest does not start, run `vm start guest0` with the verbose flag (`vm -f start guest0` keeps it in the foreground), look at the error message, and consult the `bhyve(8)` manual. The most common issues are missing resources (disk image path wrong, switch not set up) and permissions (user not in the `vm` group, or directories not readable).

If networking does not work inside the guest, check the bridge on the host (`ifconfig bridge0`), check the tap device (`ifconfig tapN`), and check that the VM's `network0` setting matches the switch name. `vm-bhyve` generates a tap device per VM and wires it into the specified switch.

### Using This Setup for the Labs

With a working host and guest, Lab 1 is essentially done (you have a VirtIO-using guest). Lab 2 and Lab 3 can be performed inside the guest. Lab 4 needs `if_epair` on the host; Lab 5 needs a spare PCI device and IOMMU-enabled firmware. Lab 6 (building vtedu) is done inside the guest. Lab 7 (measuring overhead) uses the guest as the test subject.

In short: if your `vm-bhyve` setup is solid, the rest of the chapter's hands-on work is accessible.

## Appendix: VirtIO Feature Bits Reference

This appendix collects the VirtIO feature bits most likely to matter to a driver author, with a brief description of each. The authoritative source is the VirtIO specification; this is a digest for quick reference.

### Device-Independent Features

These apply across all VirtIO device types.

- `VIRTIO_F_NOTIFY_ON_EMPTY` (bit 24): The device should notify the driver when the virtqueue becomes empty, in addition to normal completion notifications. Useful for drivers that want to know when all outstanding requests have been processed.

- `VIRTIO_F_ANY_LAYOUT` (bit 27, deprecated in v1): Headers and data can be in any scatter-gather layout. Always negotiated in v1 drivers; not relevant for modern code.

- `VIRTIO_F_RING_INDIRECT_DESC` (bit 28): Indirect descriptors supported. An entry in the descriptor table can point to another table, allowing longer scatter-gather lists without expanding the main ring. Strongly recommended for drivers that handle large requests.

- `VIRTIO_F_RING_EVENT_IDX` (bit 29): Event-index interrupt suppression. Lets the driver tell the device "do not interrupt me before descriptor N is available", reducing interrupt rate. Strongly recommended for high-rate drivers.

- `VIRTIO_F_VERSION_1` (bit 32): Modern VirtIO (version 1.0 or later). Without this, the driver is in legacy mode, with different config-space layout and conventions. New drivers should require this.

- `VIRTIO_F_ACCESS_PLATFORM` (bit 33): The device uses a platform-specific DMA address translation (for example, an IOMMU). Required for passthrough-capable deployments.

- `VIRTIO_F_RING_PACKED` (bit 34): Packed virtqueue layout. A newer, more cache-friendly layout than the classic split layout. Not supported by all backends; negotiate but do not require.

- `VIRTIO_F_IN_ORDER` (bit 35): Descriptors are used in the same order they were made available. Allows optimisations in the driver (no need to track descriptor indexes); not supported by all backends.

### Block Device Features (`virtio_blk`)

- `VIRTIO_BLK_F_SIZE_MAX` (bit 1): The device has a maximum single-request size.
- `VIRTIO_BLK_F_SEG_MAX` (bit 2): The device has a maximum segment count per request.
- `VIRTIO_BLK_F_GEOMETRY` (bit 4): The device reports its cylinder/head/sector geometry. Mostly legacy.
- `VIRTIO_BLK_F_RO` (bit 5): The device is read-only.
- `VIRTIO_BLK_F_BLK_SIZE` (bit 6): The device reports its block size.
- `VIRTIO_BLK_F_FLUSH` (bit 9): The device supports flush (fsync) commands.
- `VIRTIO_BLK_F_TOPOLOGY` (bit 10): The device reports topology information (alignment, etc.).
- `VIRTIO_BLK_F_CONFIG_WCE` (bit 11): The driver can query and set the write-cache-enabled flag.
- `VIRTIO_BLK_F_DISCARD` (bit 13): The device supports discard (trim) commands.
- `VIRTIO_BLK_F_WRITE_ZEROES` (bit 14): The device supports write-zeroes commands.

### Network Device Features (`virtio_net`)

- `VIRTIO_NET_F_CSUM` (bit 0): The device can offload checksum computation.
- `VIRTIO_NET_F_GUEST_CSUM` (bit 1): The driver can checksum-offload incoming packets.
- `VIRTIO_NET_F_MAC` (bit 5): The device provides a MAC address.
- `VIRTIO_NET_F_GSO` (bit 6): GSO (generic segmentation offload) supported.
- `VIRTIO_NET_F_GUEST_TSO4` (bit 7): Receive-side TSO over IPv4.
- `VIRTIO_NET_F_GUEST_TSO6` (bit 8): Receive-side TSO over IPv6.
- `VIRTIO_NET_F_GUEST_ECN` (bit 9): ECN (explicit congestion notification) supported on receive.
- `VIRTIO_NET_F_GUEST_UFO` (bit 10): Receive-side UFO (UDP fragmentation offload).
- `VIRTIO_NET_F_HOST_TSO4` (bit 11): Transmit-side TSO over IPv4.
- `VIRTIO_NET_F_HOST_TSO6` (bit 12): Transmit-side TSO over IPv6.
- `VIRTIO_NET_F_HOST_ECN` (bit 13): ECN on transmit.
- `VIRTIO_NET_F_HOST_UFO` (bit 14): Transmit-side UFO.
- `VIRTIO_NET_F_MRG_RXBUF` (bit 15): Merged receive buffers.
- `VIRTIO_NET_F_STATUS` (bit 16): Configuration status is supported.
- `VIRTIO_NET_F_CTRL_VQ` (bit 17): Control virtqueue.
- `VIRTIO_NET_F_CTRL_RX` (bit 18): Control channel for receive mode filtering.
- `VIRTIO_NET_F_CTRL_VLAN` (bit 19): Control channel for VLAN filtering.
- `VIRTIO_NET_F_MQ` (bit 22): Multiqueue support.
- `VIRTIO_NET_F_CTRL_MAC_ADDR` (bit 23): Control channel for MAC-address setting.

### How to Read a Feature Word

Features are a 64-bit word, with bits as described. To check whether a feature is negotiated:

```c
if ((sc->features & VIRTIO_F_RING_EVENT_IDX) != 0) {
	/* event indexes are available */
}
```

To advertise a feature for negotiation, `|` it into the features mask before calling `virtio_negotiate_features`. The post-negotiation value of `sc->features` is the intersection of what the driver requested and what the backend offers.

### Common Pitfalls

- Hard-coding feature requirements. A driver that requires `VIRTIO_NET_F_MRG_RXBUF` and falls over if it is absent will not work with simpler backends. Prefer to negotiate optimistically and adapt to what you get.
- Forgetting to check the post-negotiation features. A driver that behaves as if a feature is always present, without checking `sc->features`, will mis-program the device when the feature is absent.
- Ignoring version 1 requirements. Modern code should require `VIRTIO_F_VERSION_1`; legacy mode has too many quirks to be worth supporting in new drivers.

### A Useful Habit

Log the negotiated feature word at attach time, with each interesting bit named. The `device_printf` below does this succinctly.

```c
device_printf(dev, "features: ver1=%d evt_idx=%d indirect=%d mac=%d\n",
    (sc->features & VIRTIO_F_VERSION_1) != 0,
    (sc->features & VIRTIO_F_RING_EVENT_IDX) != 0,
    (sc->features & VIRTIO_F_RING_INDIRECT_DESC) != 0,
    (sc->features & VIRTIO_NET_F_MAC) != 0);
```

This single line in `dmesg` at attach time tells you, for any bug report, what feature set the driver is operating on. It is the VirtIO equivalent of logging a hardware revision; indispensable for support.

## Appendix: Further Reading

For readers who want to go deeper, here is a short curated list.

### FreeBSD source tree

- `/usr/src/sys/dev/virtio/random/virtio_random.c` - The smallest complete
  VirtIO driver. Read first.
- `/usr/src/sys/dev/virtio/network/if_vtnet.c` - A larger VirtIO driver.
- `/usr/src/sys/dev/virtio/block/virtio_blk.c` - A request-response VirtIO
  driver.
- `/usr/src/sys/dev/virtio/virtqueue.c` - The ring machinery.
- `/usr/src/sys/amd64/vmm/` - The bhyve kernel module.
- `/usr/src/usr.sbin/bhyve/` - The bhyve user-space emulator.
- `/usr/src/sys/kern/kern_jail.c` - Jail implementation.
- `/usr/src/sys/net/vnet.h`, `/usr/src/sys/net/vnet.c` - VNET framework.
- `/usr/src/sys/net/if_tuntap.c` - A VNET-aware cloning pseudo-driver.

### Manual pages

- `virtio(4)`, `vtnet(4)`, `virtio_blk(4)`
- `bhyve(8)`, `bhyvectl(8)`, `vmm(4)`, `vmm_dev(4)`
- `pci_passthru(4)`
- `jail(8)`, `jail.conf(5)`, `jexec(8)`, `jls(8)`
- `devfs(5)`, `devfs.rules(5)`, `devfs(8)`
- `rctl(8)`, `racct(9)`
- `priv(9)`, `ucred(9)`
- `if_epair(4)`, `vlan(4)`, `tun(4)`, `tap(4)`

### External standards

- The VirtIO 1.2 specification (OASIS) is the authoritative reference for
  the protocol. Available from the OASIS VirtIO Technical Committee site.
- The PCI-SIG specifications for PCI Express, MSI-X, and ACS are relevant
  for passthrough.

### FreeBSD Handbook

- Chapter on jails, which complements this chapter with an administrative
  perspective.
- Chapter on virtualisation, which covers `bhyve(8)` management in more
  depth than this driver-focused chapter.

These resources together form a reading programme that will carry a
motivated reader from this chapter's introduction to a practical fluency
in FreeBSD virtualisation and containerisation. You do not need to read
them all; pick the ones that fit your current project and build from
there.

## Appendix: Anti-Patterns in Virtualised Drivers

A good way to learn a craft is to study its common mistakes. This appendix collects the anti-patterns we have seen throughout the chapter, in a single place, with the fix for each. When reviewing a driver (your own or someone else's), scan for these patterns; each is a reliable sign of trouble.

### Busy-Waiting on a Status Register

```c
/* Anti-pattern */
while ((bus_read_4(sc->res, STATUS) & READY) == 0)
	;
```

Under virtualisation, each bus read is a VM exit. A tight loop consumes enormous CPU time and may never terminate if the guest is scheduled out. Use `DELAY(9)` inside the loop and a bounded iteration count, or use an interrupt-driven design that does not poll at all.

### Caching Physical Addresses

```c
/* Anti-pattern */
uint64_t phys_addr = vtophys(buffer);
bus_write_8(sc->res, DMA_ADDR, phys_addr);
/* ...later... */
bus_write_8(sc->res, DMA_ADDR, phys_addr);  /* still valid? */
```

A physical address is a temporary view. Under memory compaction, live migration, or memory hotplug, the address may no longer refer to the same physical memory. Use `bus_dma(9)` and hold a DMA map; the map tracks the bus address correctly across kernel memory operations.

### Ignoring `si_drv1`

```c
/* Anti-pattern */
static int
mydrv_read(struct cdev *dev, struct uio *uio, int flags)
{
	struct mydrv_softc *sc = devclass_get_softc(mydrv_devclass, 0);
	/* what if there are multiple units? */
}
```

The `dev->si_drv1` slot is there to connect the cdev back to its softc. Setting it in attach and using it in read/write/ioctl is the idiomatic pattern. Using `devclass_get_softc` with a hardcoded unit number is a minefield as soon as more than one instance attaches.

### Assuming INTx

```c
/* Anti-pattern */
sc->irq_rid = 0;
sc->irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ, &sc->irq_rid,
    RF_SHAREABLE | RF_ACTIVE);
```

INTx is slower under virtualisation and does not scale to many-queue devices. Use `pci_alloc_msix` first, fall back to `pci_alloc_msi`, and only fall back to INTx for hardware that does not support message-signalled interrupts.

### Hard-Coding Feature Bits

```c
/* Anti-pattern */
if ((sc->features & VIRTIO_F_RING_INDIRECT_DESC) == 0)
	panic("device does not support indirect descriptors");
```

A driver that panics when a feature is missing is impossible to use with a backend that does not advertise the feature. Negotiate optimistically, check the post-negotiation result, and fall back gracefully to a less efficient path if the feature is missing.

### Sleep-With-Spin-Lock Held

```c
/* Anti-pattern */
mtx_lock(&sc->lock);
tsleep(sc, PWAIT, "wait", hz);
mtx_unlock(&sc->lock);
```

`tsleep` with an `mtx(9)` spin-lock held causes a panic on FreeBSD. Use `mtx_sleep` (which drops the mutex around the sleep) or use a blocking lock (`sx(9)`) when you need to sleep.

### Returning ENOTTY for Unsupported Operations

```c
/* Anti-pattern */
case MYDRV_PRIV_IOCTL:
	if (cred->cr_prison != &prison0)
		return (ENOTTY);  /* hide from jails */
	...
```

`ENOTTY` means "this ioctl does not exist". It hides the operation from jailed callers, but it also breaks tooling that introspects available ioctls. Prefer `EPERM` for "this ioctl exists but you cannot use it", leaving introspection correct.

### Forgetting the Detach Path

```c
/* Anti-pattern */
static int
mydrv_detach(device_t dev)
{
	struct mydrv_softc *sc = device_get_softc(dev);
	/* release a few things and call it done */
	bus_release_resource(dev, SYS_RES_MEMORY, sc->res_rid, sc->res);
	return (0);
}
```

Missing callout drains, missing taskqueue drains, missing cdev destruction, missing mutex destruction, missing DMA map unload, missing interrupt teardown. Each becomes a leak or a crash at kldunload. The attach path is usually clean; the detach path is where bugs hide.

### Assuming `device_printf` Works Without a Device

```c
/* Anti-pattern */
static int
mydrv_modevent(module_t mod, int event, void *arg)
{
	device_printf(NULL, "load event");  /* crashes */
}
```

`device_printf` with a NULL `device_t` dereferences a null pointer. Inside module event handlers, before a device has been attached, use `printf` (with a "mydrv:" prefix to identify the source). `device_printf` is for per-device events after attach.

### Not Cleaning Up VNET State

```c
/* Anti-pattern */
static int
mydrv_mod_event(module_t mod, int event, void *arg)
{
	if (event == MOD_UNLOAD)
		free(mydrv_state, M_DEVBUF);  /* in which VNET? */
}
```

Per-VNET state is allocated in a specific VNET context. Freeing it without setting that context accesses the wrong VNET's data. Use `VNET_FOREACH` and `CURVNET_SET` to walk every VNET and clean up each one's state.

### Using `getnanouptime` for Timing

```c
/* Anti-pattern */
struct timespec ts;
getnanouptime(&ts);
/* ...do work... */
struct timespec ts2;
getnanouptime(&ts2);
/* difference is arbitrary inside a VM */
```

`getnanouptime` returns a low-resolution reading that the kernel caches. For precise short-duration timing, use `binuptime(9)` or `sbinuptime(9)`, which read the high-resolution time source. Under virtualisation, the precise readings are as correct as the time source allows; the cached readings have been stale for up to a tick.

### Probing Hardware in the Probe Method

```c
/* Anti-pattern */
static int
mydrv_probe(device_t dev)
{
	/* ...read status register... */
	return (BUS_PROBE_DEFAULT);
}
```

The probe method should be purely identity-based: inspect the PNP information, return a priority, and do nothing that requires the hardware to be present and functional. Hardware interaction belongs in attach. Under virtualisation, a probe that pokes hardware before feature negotiation can mis-identify the device.

### Storing Kernel Pointers Through `ioctl`

```c
/* Anti-pattern */
case MYDRV_GET_PTR:
	*(void **)data = sc->internal_state;
	return (0);
```

Passing kernel pointers to user space is a security bug. Under virtualisation it can even leak hypervisor-relevant information. Always copy the data the caller asks for, never the pointer.

### Summary

These anti-patterns cover most of the ways that FreeBSD drivers go wrong under virtualisation. They are not unique to VMs, but they are *amplified* under virtualisation: the same bug that corrupts memory once a day on bare metal might corrupt it hundreds of times per second under a VM whose timing is different. Fixing these patterns is not "fixing the VM case"; it is fixing the driver to meet the kernel's baseline expectations.

## Appendix: A Checklist for a Virtualisation-Ready Driver

A concrete checklist you can apply to your own driver before declaring it virtualisation-ready. Go through each item; any "no" answer is a task.

### Device Binding and Probe

- Does the driver use `VIRTIO_SIMPLE_PNPINFO` or `VIRTIO_DRIVER_MODULE` rather than hand-rolling PNP entries?
- Does the probe method avoid hardware access and only use PNP identity?
- Does the attach method call `virtio_negotiate_features` and log the result?
- Does attach fail cleanly (releasing every resource it has allocated) if any step after virtqueue allocation fails?

### Resources

- Does the driver use `bus_alloc_resource_any` with a dynamic RID rather than hard-coding resource IDs?
- Does the driver use `pci_alloc_msix` (or `pci_alloc_msi`) in preference to INTx?
- Are all `bus_alloc_resource` calls matched by `bus_release_resource` in detach?

### DMA

- Does every address programmed into hardware come from `bus_dma_load` (or similar) rather than `vtophys`?
- Does the driver hold DMA tags and maps with appropriate lifetimes, not re-creating them per operation when avoidable?
- Does the driver handle `bus_dma_load_mbuf_sg` correctly for scatter-gather?
- Is the driver's `bus_dma_tag` created with the correct alignment, boundary, and maximum-segment constraints?

### Interrupts

- Is the interrupt handler MP-safe (declared with `INTR_MPSAFE`)?
- Does the handler handle the "no work" case gracefully (in case of spurious wake-up)?
- Does the handler disable and re-enable virtqueue interrupts correctly, using the `virtqueue_disable_intr` / `virtqueue_enable_intr` pattern?
- Is the interrupt path free of blocking operations (no `malloc(M_WAITOK)`, no `mtx_sleep`)?

### Character Device Interface

- Does attach set `cdev->si_drv1 = sc`?
- Does detach call `destroy_dev` before freeing the softc?
- Do read and write handle short I/O correctly (less than the full buffer size)?
- Does the driver check the result of `uiomove` for errors?
- Does ioctl use `priv_check` for operations that require elevated privilege?

### Locking

- Is every softc access covered by the softc's mutex?
- Is the detach path drain-then-free, not free-then-drain?
- Are sleeping waits done with `mtx_sleep` or `sx(9)` rather than `tsleep` with a mutex held?
- Is there a discernible ordering of acquisitions (to avoid deadlock)?

### Timing

- Does the driver use `DELAY(9)`, `pause(9)`, or `callout(9)` rather than busy loops?
- Does the driver avoid reading the TSC directly?
- Does every wait have a bounded timeout, with a sensible error on exceed?

### Privilege

- Does the driver call `priv_check` for all operations that should not be available to unprivileged users?
- Does the driver use the correct privilege (`PRIV_DRIVER`, `PRIV_IO`, not `PRIV_ROOT`)?
- Does the driver consider jailed callers, using `priv_check` which also calls `prison_priv_check`?

### Detach and Unload

- Does detach drain all callouts (`callout_drain`)?
- Does detach drain all taskqueues (`taskqueue_drain_all`)?
- Does detach stop any kernel threads the driver has created (via condition variables or similar)?
- Does detach release every resource attach allocated?
- Can the module be unloaded at any time (no hangs on `kldunload`)?

### VNET (if applicable)

- Does the driver register VNET sysinit/sysuninit for per-VNET state?
- Does the driver use `V_` prefix macros for per-VNET variables?
- Does the VNET move path allocate state on entry and free on exit?
- Does the driver use `CURVNET_SET` / `CURVNET_RESTORE` when accessing a VNET other than the current one?

### Testing

- Does the driver have a `make test` target (even if it just builds and loads)?
- Has the driver been run through the attach-detach cycle at least 100 times?
- Has the driver been loaded under both VirtIO and passthrough (if applicable)?
- Has the driver been loaded inside at least one jail and one VNET jail?
- Has the driver been built on at least amd64; ideally also on arm64?

### Documentation

- Is there a README explaining what the driver does and how to build it?
- Is there a manual page describing the driver's user-visible interface?
- Is the PNP table complete enough that `kldxref` finds the driver for auto-loading?

A driver that passes this checklist is well on its way to being virtualisation-ready. A driver that fails several items needs attention before it will behave well under the diverse environments of modern deployment. Run through the list for every driver you write; it is faster than debugging each issue individually when the driver hits a customer.

## Appendix: Sketching a bhyve Backend for vtedu

Challenge 5 asks the reader to write a `bhyve(8)` backend for the `vtedu` driver. This appendix sketches the architecture of such a backend at a level of detail useful for planning. It is not a complete implementation; writing one is the challenge. The goal here is to demystify the backend side of the VirtIO story so the challenge becomes tractable.

### Where the Code Lives

The `bhyve(8)` user-space emulator lives under `/usr/src/usr.sbin/bhyve/`. Its source files are a mixture of CPU and chipset emulation, per-device emulators for different VirtIO types, and glue code that connects `bhyve(8)` to the `vmm(4)` kernel module. The relevant per-device files for VirtIO are:

- `/usr/src/usr.sbin/bhyve/pci_virtio_rnd.c`: virtio-rnd (random number generator). The simplest VirtIO backend. Read first.
- `/usr/src/usr.sbin/bhyve/pci_virtio_block.c`: virtio-blk (block device).
- `/usr/src/usr.sbin/bhyve/pci_virtio_net.c`: virtio-net (network).
- `/usr/src/usr.sbin/bhyve/pci_virtio_9p.c`: virtio-9p (filesystem share).
- `/usr/src/usr.sbin/bhyve/pci_virtio_console.c`: virtio-console (serial).

Each of these is a few hundred to a couple of thousand lines of code. They share a common backend framework in `/usr/src/usr.sbin/bhyve/virtio.h` and `/usr/src/usr.sbin/bhyve/virtio.c`. The `pci_virtio_*.c` files above are per-device consumers of that framework; each one registers a `struct virtio_consts`, a set of virtqueue callbacks, and the device-specific config-space layout.

### The Framework Handles the Protocol

The good news for a backend author is that `bhyve(8)` already implements the VirtIO protocol. Feature negotiation, descriptor-ring management, notification delivery, interrupt injection, all of it lives in the `virtio.c` framework. A new backend implements only the device-specific behaviour: what happens when a buffer arrives on the virtqueue, what config-space fields the device exposes, and how device-level events are generated.

The framework-facing interface is a small set of callbacks, encapsulated in a `struct virtio_consts`.

```c
/* Sketch, not actual bhyve code. */
struct virtio_consts {
	const char *vc_name;
	int vc_nvq;
	size_t vc_cfgsize;
	void (*vc_reset)(void *);
	void (*vc_qnotify)(void *, struct vqueue_info *);
	int (*vc_cfgread)(void *, int, int, uint32_t *);
	int (*vc_cfgwrite)(void *, int, int, uint32_t);
	void (*vc_apply_features)(void *, uint64_t);
	uint64_t vc_hv_caps;
};
```

A new backend fills in this struct and registers it with the framework. The framework calls into the backend when the guest-side driver does interesting things (resets the device, notifies the virtqueue, reads or writes config space).

### Sketching the vtedu Backend

For `vtedu`, the backend is simple. It has one virtqueue, no config-space fields beyond the generic ones, and one feature bit (`VTEDU_F_UPPERCASE`). Its state is:

```c
struct pci_vtedu_softc {
	struct virtio_softc vsc_vs;
	struct vqueue_info vsc_vq;  /* just one queue */
	pthread_mutex_t vsc_mtx;
	uint64_t vsc_features;
};
```

The callbacks are small.

```c
static void
pci_vtedu_reset(void *vsc)
{
	struct pci_vtedu_softc *sc = vsc;

	pthread_mutex_lock(&sc->vsc_mtx);
	vi_reset_dev(&sc->vsc_vs);
	sc->vsc_features = 0;
	pthread_mutex_unlock(&sc->vsc_mtx);
}

static void
pci_vtedu_apply_features(void *vsc, uint64_t features)
{
	struct pci_vtedu_softc *sc = vsc;

	pthread_mutex_lock(&sc->vsc_mtx);
	sc->vsc_features = features;
	pthread_mutex_unlock(&sc->vsc_mtx);
}
```

The interesting callback is `vc_qnotify`, called when the guest notifies the virtqueue.

```c
static void
pci_vtedu_qnotify(void *vsc, struct vqueue_info *vq)
{
	struct pci_vtedu_softc *sc = vsc;
	struct iovec iov[1];
	uint16_t idx;
	int n;

	while (vq_has_descs(vq)) {
		n = vq_getchain(vq, &idx, iov, 1, NULL);
		if (n < 1) {
			EPRINTLN("vtedu: empty chain");
			vq_relchain(vq, idx, 0);
			continue;
		}

		if ((sc->vsc_features & VTEDU_F_UPPERCASE) != 0) {
			for (int i = 0; i < iov[0].iov_len; i++) {
				uint8_t *b = iov[0].iov_base;
				if (b[i] >= 'a' && b[i] <= 'z')
					b[i] = b[i] - 'a' + 'A';
			}
		}

		vq_relchain(vq, idx, iov[0].iov_len);
	}

	vq_endchains(vq, 1);
}
```

That is the core of it. The `vq_has_descs`, `vq_getchain`, `vq_relchain`, and `vq_endchains` calls are framework helpers that unpack the virtqueue descriptors into `iovec` structures and repackage the results.

### Connecting to the Device Table

`bhyve(8)` maintains a table of device emulators; each backend registers itself at build time. The registration uses a `PCI_EMUL_TYPE(...)` macro (or similar) that adds the backend's vtable to a linker-set. Once registered, the `bhyve(8)` command line can reference the backend by name:

```sh
bhyve ... -s 7,virtio-edu guest0
```

The `-s 7,virtio-edu` adds the `virtio-edu` device at PCI slot 7. When the guest boots, the FreeBSD kernel enumerates the PCI bus, finds the device with VirtIO vendor ID and the right device ID, and attaches `vtedu` to it.

### What the Backend Must Verify

To make the backend correct, the author must verify:

- The VirtIO device ID matches the driver's expectation (`VIRTIO_ID_EDU = 0xfff0`).
- The feature-negotiation response matches what the driver expects (advertise `VTEDU_F_UPPERCASE` and `VIRTIO_F_VERSION_1`).
- The virtqueue sizes are large enough for the driver's workload (256 is a reasonable default).
- The config-space size matches what the driver reads (zero for `vtedu`).

### Testing the End-to-End Loop

With both sides in place, the end-to-end test looks like this.

On the host, after building the backend and installing the modified `bhyve(8)`:

```sh
sudo bhyve ... -s 7,virtio-edu guest0
```

Inside the guest, after copying and building `vtedu.c`:

```sh
sudo kldload ./virtio_edu.ko
ls /dev/vtedu0
echo "hello world" > /dev/vtedu0
cat /dev/vtedu0
```

The expected output of `cat` is `HELLO WORLD` (uppercased by the backend).

If the output is `hello world` (not uppercased), the backend did not negotiate `VTEDU_F_UPPERCASE`. If the `echo` hangs, the virtqueue notification is not reaching the backend. If the `cat` hangs, the backend's interrupt injection is not reaching the guest. Each of these is a specific failure that the debugging techniques from the chapter can localise.

### Why This Exercise Is Worth the Effort

Writing a backend and a driver teaches both sides of the VirtIO story. The driver side is what most authors eventually write, but the backend side tells you *why* the VirtIO protocol is shaped the way it is. Feature bits make sense when you have to decide which to advertise. Virtqueue descriptors make sense when you have to unpack them. Interrupt delivery makes sense when you have to inject one.

Completing Challenge 5 promotes you from "VirtIO user" to "VirtIO author", which is a different level of understanding. If the challenge feels large, tackle it in stages: first get the device to appear in the guest (verify with `pciconf -lv`), then get feature negotiation to work, then handle a single virtqueue message, then polish the full pipeline. Each stage is a separate commit and a separate satisfying milestone.

This is the end of the chapter's technical material. The remaining prose ties together what you have learned and points forward to Chapter 31.

## Appendix: Running the Chapter's Techniques in CI

Continuous integration is now a standard part of most driver projects. This short appendix describes how the chapter's techniques fit into a CI pipeline. The goal is to show that virtualisation and containerisation are not just runtime concerns; they are also practical tools for keeping a driver honest across changes.

### Why CI Benefits from Virtualisation

A CI system is, among other things, a place where you need reproducible test environments. Running tests on bare metal is possible but fragile: the test machine accumulates state, different machines have different hardware, and failures are hard to separate from hardware quirks. Running tests inside a VM removes most of these problems. The VM is a clean slate at the start of each run, its "hardware" is uniform across test runs, and its failures are the driver's failures, not the host's.

For FreeBSD driver CI, the standard approach is to run a FreeBSD guest under `bhyve(8)` (if the CI host is FreeBSD) or under KVM/QEMU (if the CI host is Linux). The guest boots a FreeBSD image, loads the driver, runs the test harness, and exits. The whole cycle takes under a minute for a small driver, which means hundreds of tests per day against every commit.

### A Minimal CI Flow for a VirtIO Driver

A reasonable flow for a VirtIO driver's CI is:

1. Check out the driver source.
2. Build the driver against the target FreeBSD kernel (often using a cross-compile on the CI host).
3. Start a FreeBSD VM with appropriate VirtIO devices.
4. Copy the built module into the VM.
5. SSH into the VM and load the module.
6. Run the test harness.
7. Capture the output.
8. Shut down the VM.
9. Report pass/fail.

Steps 1 and 2 are unchanged from a non-VM workflow. Steps 3 through 9 are what virtualisation adds.

### Practical Tools

For step 3, `vm-bhyve` is convenient on FreeBSD hosts. For Linux CI hosts, `virt-install` from libvirt is a standard tool. Both produce a running VM in a few seconds with a pre-built image.

For step 4, a shared volume or a small SSH copy is usual. `virtfs` (9P) or `virtiofs` pass host directories into the guest; `scp` over a tap interface works as well.

For step 5, pre-installed SSH keys and a static IP address (or a DHCP reservation) make the connection painless.

For steps 6 and 7, the test harness is whatever the driver author writes: a shell script, a C program, a Python harness. Whatever it is, it runs inside the VM.

For step 8, `vm stop guest0 --force` (or equivalent) shuts the VM down rapidly. The image is discarded; the next run starts fresh.

For step 9, the exit code of the test harness determines pass/fail. CI systems expect zero for success and nonzero for failure; be consistent.

### A Minimal Test Harness

A simple pass/fail harness for a VirtIO driver might look like this.

```sh
#!/bin/sh
set -e

# Inside the guest.  Expects the module at /tmp/mydriver.ko.

kldload /tmp/mydriver.ko

# Wait for the device to attach.
for i in 1 2 3 4 5; do
	if [ -c /dev/mydev0 ]; then
		break
	fi
	sleep 1
done

if [ ! -c /dev/mydev0 ]; then
	echo "FAIL: /dev/mydev0 did not appear"
	exit 1
fi

# Exercise the device.
echo "hello" > /dev/mydev0
output=$(cat /dev/mydev0)
if [ "$output" != "hello" ]; then
	echo "FAIL: expected 'hello', got '$output'"
	exit 1
fi

# Clean up.
kldunload mydriver

echo "PASS"
exit 0
```

Short, readable, and reports a clear result. CI scales with tests like this: add one per feature, run them all on every commit.

### Scaling to Multiple Configurations

A CI pipeline can run the same driver under multiple configurations by spinning up multiple VMs. Useful axes include:

- Kernel version (FreeBSD 14.3, 14.2, 13.5, -CURRENT).
- Architecture (amd64, arm64 with an arm64 guest).
- VirtIO feature set (force-disable certain features on the backend to exercise fallback paths).
- Hypervisor (bhyve, QEMU/KVM, VMware) where support varies.

Each configuration is an independent job that the CI system parallelises. The aggregate of "driver passes on every configuration" is a strong signal that the driver is robust.

### A Note on Hardware-in-the-Loop CI

For drivers that talk to real hardware, CI needs a bare-metal or passthrough setup. This is more expensive and less common, but some projects maintain a small fleet of test machines for this purpose. The techniques from the chapter apply: a hardware test rig uses `ppt(4)` passthrough to give a guest access to a specific device, and the CI system drives the guest the same way it would drive a pure-VirtIO guest.

Hardware CI is slower to set up and more expensive to run. For most projects, pure-VirtIO CI is enough for the bulk of tests, with a small suite of hardware tests run at a slower cadence.

### The Payoff

CI that exercises a driver under realistic conditions catches regressions quickly, while the fix is still fresh in the author's mind. A regression caught at commit-time takes minutes to fix; a regression caught weeks later during a release candidate takes hours. Virtualisation makes the former affordable, and that is one of the strongest arguments for taking the techniques in this chapter seriously.

## Appendix: Commands Cheat Sheet

A compact list of the commands a driver author uses most often when working with virtualisation and containerisation on FreeBSD. Keep this page open while working through the labs.

### Host-Side Virtualisation

```sh
# Check hypervisor extensions
sysctl hw.vmm

# Load/unload vmm(4)
kldload vmm
kldunload vmm

# vm-bhyve guest management
vm list
vm start guest0
vm stop guest0
vm console guest0
vm configure guest0
vm install guest0 /path/to/iso
vm create -t default -s 20G guest0

# Direct bhyve (verbose but informative)
bhyvectl --vm=guest0 --destroy
bhyvectl --vm=guest0 --suspend=normal
```

### Guest-Side Inspection

```sh
# Is this a guest?
sysctl kern.vm_guest

# Which devices attached?
pciconf -lvBb
devinfo -v
kldstat -v

# VirtIO-specific
dmesg | grep -i virtio
sysctl dev.virtio_pci
sysctl dev.virtqueue
```

### PCI Passthrough

```sh
# Mark a device as passthrough-capable (in /boot/loader.conf)
pptdevs="5/0/0"

# Verify after reboot
pciconf -lvBb | grep ppt
dmesg | grep -i dmar      # Intel
dmesg | grep -i amdvi     # AMD
```

### Jails

```sh
# Create and manage jails
jail -c name=test path=/jails/test host.hostname=test ip4=inherit persist
jls
jexec test /bin/sh
jail -r test

# devfs rulesets
devfs rule -s 100 show
devfs -m /jails/test/dev rule -s 100 applyset
```

### VNET

```sh
# epair setup
kldload if_epair
ifconfig epair create
ifconfig epair0a 10.0.0.1/24 up

# Move one end into a VNET jail
jail -c name=vnet-test vnet vnet.interface=epair0b path=/jails/vnet-test persist

# Confirm
ifconfig -j vnet-test epair0b
```

### Resource Limits

```sh
# rctl
rctl -a jail:test:memoryuse:deny=512M
rctl -a jail:test:pcpu:deny=50
rctl -h jail:test
rctl -l jail:test
```

### Observability

```sh
# Interrupt rate
vmstat -i

# VM exit counts (needs PMC)
pmcstat -S VM_EXIT -l 10

# DTrace virtqueue activity
dtrace -n 'fbt::virtqueue_enqueue:entry { @[probefunc] = count(); }'

# Kernel trace
ktrace -i -p $(pgrep bhyve)
kdump -p $(pgrep bhyve)
```

### Module Lifecycle

```sh
# Build, load, test, unload
make clean && make
sudo kldload ./mydriver.ko
kldstat -v | grep mydriver
# ...exercise driver...
sudo kldunload mydriver
```

These are the day-to-day commands. A quick-reference card like this, pinned to a wall or kept in a terminal tab, saves hours over the course of a project.

With that, the chapter is complete.
