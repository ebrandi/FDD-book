---
title: "Integrating with the Kernel"
description: "Chapter 24 extends the myfirst driver from 1.6-debug to 1.7-integration. It teaches what it means for a driver to stop being a self-contained kernel module and to start behaving as a citizen of the wider FreeBSD kernel. The chapter explains why integration matters; how a driver lives inside devfs and how /dev nodes appear, get permissions, are renamed, and disappear; how to implement an ioctl() interface that user space can rely on, including the _IO/_IOR/_IOW/_IOWR encoding and the kernel's automatic copyin/copyout layer; how to expose driver metrics, counters, and tunable knobs through dynamic sysctl trees rooted under dev.myfirst.N.; how to think about hooking a driver into the network stack through ifnet(9) at an introductory level using if_tuntap.c as the reference; how to think about hooking a driver into the CAM storage subsystem at an introductory level using cam_sim_alloc and xpt_bus_register; how to organise registration, attachment, teardown, and cleanup so the integrated paths can be loaded and unloaded cleanly under stress; and how to refactor the driver into a maintainable, versioned package that future chapters can keep extending. The driver gains myfirst_ioctl.c, myfirst_ioctl.h, myfirst_sysctl.c, and a small companion test program; gains a clone-aware /dev/myfirst0 node with a per-instance sysctl subtree; and leaves Chapter 24 as a driver that other software can talk to in the FreeBSD-native way."
partNumber: 5
partName: "Debugging, Tools, and Real-World Practices"
chapter: 24
lastUpdated: "2026-04-19"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "TBD"
estimatedReadTime: 210
---

# Integrating with the Kernel

## Reader Guidance & Outcomes

Chapter 23 closed with a driver that can finally explain itself. The `myfirst` driver at version `1.6-debug` knows how to log structured messages through `device_printf`, how to gate verbose output through a runtime sysctl mask, how to expose static probe points to DTrace, and how to leave a paper trail an operator can read back later. Combined with the power discipline added in Chapter 22, the DMA pipeline added in Chapter 21, and the interrupt machinery added in Chapter 19 and Chapter 20, the driver is now a complete unit in itself: it boots, it runs, it talks to a real PCI device, it survives suspend and resume, and it tells the developer what it is doing along the way.

What the driver does not yet do is behave like a citizen of the wider kernel. There is still very little of `myfirst` that an outside program can see, control, or measure. The driver creates exactly one device node when the module loads. There is no way for a user-space tool to ask the driver to reset itself, to flip a configuration knob at runtime, or to read out a counter. There are no metrics in the system's sysctl tree that an operator could feed to a monitoring system. There is no way to give the driver several instances cleanly. There is no integration with the network stack, with the storage stack, or with any of the kernel subsystems users routinely reach for from their own programs. In every meaningful sense, the driver is still standing alone in the corner. Chapter 24 is the chapter that brings it into the room.

Chapter 24 teaches kernel integration at the right level for this stage of the book. The reader will spend this chapter learning what integration actually means, why it matters more than it looks at first, and how each integration surface is built. The chapter starts with the conceptual story: the difference between a working driver and an integrated driver, and the cost of leaving integration as an afterthought. It then spends most of its time on the four interfaces a typical FreeBSD driver always integrates with: the device file system `devfs`, the user-controlled `ioctl(2)` channel, the system-wide `sysctl(8)` tree, and the kernel's lifecycle hooks for clean attach, detach, and module unload. After those four come two optional chapters in miniature, one for drivers whose hardware is a network device and one for drivers whose hardware is a storage controller. Both are introduced at the conceptual level so the reader recognises them when they appear later in Part 6 and Part 7, and neither is taught in full because each will eventually deserve its own chapter. The chapter then steps back to discuss the discipline of registration and teardown across all of these surfaces: the order matters, the failure paths matter, the corner cases under stress matter, and a driver that gets the integration interfaces right but the lifecycle wrong is still a fragile driver. Finally the chapter closes with a refactor that splits the new code into its own files, bumps the driver to version `1.7-integration`, updates the version banner, and leaves the source tree organised for everything that follows.

The arc of Part 5 continues here. Chapter 22 made the driver survive a change of power state. Chapter 23 made the driver tell you what it is doing. Chapter 24 makes the driver fit naturally into the rest of the system, so that the tools and habits FreeBSD users already know carry over to your driver without surprises. Chapter 25 will continue the arc by teaching the maintenance discipline that keeps a driver readable, tunable, and extensible as it evolves, and Part 6 will then begin the transport-specific chapters that lean on every quality the Part 5 chapters have built.

### Why devfs, ioctl, and sysctl Integration Earn a Chapter of Their Own

One concern that surfaces here is whether wiring up `devfs`, `ioctl`, and `sysctl` really deserves a full chapter. The driver already has a single `cdev` node from a much earlier chapter. Adding an ioctl looks small. Adding a sysctl looks even smaller. Why spread the work over a long chapter, when each interface looks like a few dozen lines of code?

The answer is that the few-dozen-lines view is the easy part. Each interface has a set of conventions and pitfalls that are not obvious from reading the API once, and the cost of getting them wrong is paid not by the developer but by the operator who tries to monitor the driver, the user who tries to reset the device, the packager who tries to load and unload the module under load, and the next developer who tries to extend the driver six months later. Chapter 24 spends its time on those conventions and pitfalls because that is where the value is.

The first reason this chapter earns its place is that **the integration interfaces are how everything else reaches the driver**. A reader who has followed the book this far has built a driver that does interesting work, but only the kernel itself currently knows how to ask the driver to do that work. Once the driver has an ioctl interface, a shell script can drive it. Once the driver has a sysctl tree, a monitoring system can watch it. Once the driver creates `/dev` nodes that follow the standard conventions, a packager can ship udev-style rules, the system administrator can write `/etc/devfs.rules` for it, and another driver can layer on top of it through `vop_open` or through `ifnet`. None of that depends on what the driver is for; all of it depends on whether the integration is done well.

The second reason is that **integration choices show up in production failure modes**. A driver that calls `make_dev` from the wrong context can deadlock at module load. A driver that omits the `_IO`, `_IOR`, `_IOW`, `_IOWR` discipline forces every caller to invent a private convention for who copies what across the user-kernel boundary, and at least one of those callers will get it wrong. A driver that forgets to call `sysctl_ctx_free` on detach leaks OIDs, and the next module load that uses the same name fails with a confusing message. A driver that destroys its `cdev` before draining its open file handles produces use-after-free panics. Chapter 24 spends paragraphs on each of these because each is a real bug the FreeBSD community has had to chase down across the years, and the right time to learn the discipline is before writing the first line of integration code, not after.

The third reason is that **the integration code is the first place a driver's design becomes visible to someone other than its author**. Until Chapter 24 the driver was a black box with one method table and one device node. From Chapter 24 onward, the driver has a public surface. The names of its sysctls show up in monitoring graphs. The numbers of its ioctls show up in shell scripts and in user-space libraries. The layout of its `/dev` nodes shows up in package documentation and in administrator runbooks. Once a public surface exists, changing it has a cost. The chapter therefore takes care to teach the conventions that keep the surface stable as the driver evolves. The version bump to `1.7-integration` is also the first version of the driver that has a real public face; everything before it was an internal milestone.

Chapter 24 earns its place by teaching those three ideas together, concretely, with the `myfirst` driver as the running example. A reader who finishes Chapter 24 can integrate any FreeBSD driver into the standard system interfaces, knows the conventions and pitfalls of each integration surface, can read another driver's integration code and identify what is normal and what is unusual, and has a `myfirst` driver that other software can finally talk to.

### Where Chapter 23 Left the Driver

A short checkpoint before the real work starts. Chapter 24 extends the driver produced at the end of Chapter 23, tagged as version `1.6-debug`. If any of the items below is uncertain, return to Chapter 23 and fix it before starting this chapter, because the integration topics assume the debug discipline already exists, and several of the new integration surfaces will use it.

- Your driver compiles cleanly and identifies itself as `1.6-debug` in `kldstat -v`.
- The driver still does everything it did at `1.5-power`: it attaches to a PCI (or simulated PCI) device, allocates MSI-X vectors, runs a DMA pipeline, and survives `devctl suspend myfirst0` followed by `devctl resume myfirst0`.
- The driver has a `myfirst_debug.c` and `myfirst_debug.h` pair on disk. The header defines `MYF_DBG_INIT`, `MYF_DBG_OPEN`, `MYF_DBG_IO`, `MYF_DBG_IOCTL`, `MYF_DBG_INTR`, `MYF_DBG_DMA`, `MYF_DBG_PWR`, and `MYF_DBG_MEM`. The `DPRINTF(sc, MASK, fmt, ...)` macro is in scope from any source file in the driver.
- The driver has three SDT probes named `myfirst:::open`, `myfirst:::close`, and `myfirst:::io`. The simple DTrace one-liner `dtrace -n 'myfirst::: { @[probename] = count(); }'` returns counts when the device is exercised.
- The softc carries a `uint32_t sc_debug` field, and `sysctl dev.myfirst.0.debug.mask` reads and writes it.
- The driver has a `DEBUG.md` document beside the source. `HARDWARE.md`, `LOCKING.md`, `SIMULATION.md`, `PCI.md`, `INTERRUPTS.md`, `MSIX.md`, `DMA.md`, and `POWER.md` are also up to date in your working tree from earlier chapters.
- Your test kernel still has `INVARIANTS`, `WITNESS`, `WITNESS_SKIPSPIN`, `DDB`, `KDB`, `KDB_UNATTENDED`, `KDTRACE_HOOKS`, and `DDB_CTF` enabled. Chapter 24's labs use the same kernel.

That driver is what Chapter 24 extends. The additions are bigger than in Chapter 23 in code lines, but smaller in conceptual surface. The new pieces are: a richer `/dev/myfirst0` node that can clone instances on demand, a small set of well-typed ioctls with a public header that user-space programs can include, a per-instance sysctl subtree under `dev.myfirst.N.` exposing a handful of metrics and one writable knob, a refactor that splits the new code into `myfirst_ioctl.c` and `myfirst_sysctl.c` with matching headers, a small companion user-space program named `myfirstctl` that exercises the new interfaces, an `INTEGRATION.md` document beside the source, an updated regression test, and a version bump to `1.7-integration`.

### What You Will Learn

Once you have finished this chapter, you will be able to:

- Explain what kernel integration means in concrete FreeBSD terms, distinguish a self-contained driver from an integrated one, and name the specific user-visible benefits each integration surface delivers.
- Describe what `devfs` is, how it differs from older static `/dev` schemes, and how device nodes come into and out of existence under it. Use `make_dev`, `make_dev_s`, `make_dev_credf`, and `destroy_dev` correctly. Choose the right flag set, ownership, and mode for a node.
- Initialise and populate a `struct cdevsw` with the modern `D_VERSION` field, the minimal callback set, and the optional callbacks (`d_kqfilter`, `d_mmap_single`, `d_purge`).
- Use the `cdev->si_drv1` and `si_drv2` fields to attach per-node driver state, and read that state back from inside the cdevsw callbacks.
- Create more than one device node from a single driver instance and choose between fixed-name nodes, indexed nodes, and clonable nodes through the `dev_clone` event handler.
- Set per-node permissions and ownership at creation time, and adjust them after creation through `devfs.rules` so administrators can grant access without rebuilding the driver.
- Explain what `ioctl(2)` is, how the kernel encodes ioctl commands using `_IO`, `_IOR`, `_IOW`, and `_IOWR`, what each macro means about the direction of data flow, and why getting the encoding right matters for portability between 32-bit and 64-bit user space.
- Define a public ioctl header for a driver, choose a free magic letter, and document each command so that user-space callers can rely on the interface across releases.
- Implement a `d_ioctl` handler that dispatches on the command word, performs the per-command logic safely, and returns the right errno on each failure path.
- Read and understand the kernel's automatic `copyin`/`copyout` layer for ioctl data and recognise the cases where the driver still has to copy memory itself: variable-length payloads, embedded user pointers, and structures whose layout requires explicit alignment.
- Explain `sysctl(9)`, distinguish static OIDs from dynamic OIDs, and walk through the `device_get_sysctl_ctx` and `device_get_sysctl_tree` pattern that gives every device its own subtree.
- Add read-only counters using `SYSCTL_ADD_UINT` and `SYSCTL_ADD_QUAD`, add writable knobs with appropriate access flags, and add custom procedural OIDs with `SYSCTL_ADD_PROC` for cases where the value has to be computed at read time.
- Manage tunables that a user can set in `/boot/loader.conf` with `TUNABLE_INT_FETCH`, and combine tunables and sysctls so the same configuration knob can be set at boot or adjusted at runtime.
- Recognise the introductory shape of FreeBSD's networking integration: how `if_alloc`, `if_initname`, `if_attach`, `bpfattach`, `if_detach`, and `if_free` compose; what an `if_t` is at the conceptual level; and what role drivers play in the larger ifnet machinery. Understand that Chapter 28 returns to this in depth.
- Recognise the introductory shape of FreeBSD's storage integration: what CAM is, how `cam_sim_alloc`, `xpt_bus_register`, the `sim_action` callback, and `xpt_done` compose; what a CCB is conceptually; and why CAM exists at all. Understand that Chapter 27's storage drivers and Chapter 27's GEOM material return to this in depth.
- Apply a registration and teardown discipline that is robust under repeated `kldload`/`kldunload`, under attach failure midway through bringup, under detach failure when in-flight users still hold open file descriptors, and under genuine surprise removal of the underlying device.
- Refactor a driver that has accumulated several integration surfaces into a maintainable structure: a separate file per integration concern, a public header for user space, a private header for in-driver use, and an updated build system that compiles all of the pieces into a single kernel module.

The list is long because integration touches several subsystems. Each item is narrow and teachable. The chapter's work is putting them together into a single coherent driver.

### What This Chapter Does Not Cover

Several adjacent topics are explicitly deferred so Chapter 24 stays focused on integration discipline.

- **The full implementation of an ifnet network driver**, including transmit and receive queues, multi-queue coordination through `iflib(9)`, BPF integration beyond the introductory `bpfattach` call, link-state events, and the full Ethernet driver lifecycle. Chapter 28 is the dedicated network driver chapter and assumes Chapter 24's integration discipline is already in place.
- **The full implementation of a CAM storage driver**, including target mode, the full CCB type set, asynchronous notifications through `xpt_setup_ccb` and `xpt_async`, and the geometry presentation through `disk_create` or GEOM. Chapter 27 covers the storage stack in depth.
- **GEOM integration**, including providers, consumers, classes, `g_attach`, `g_detach`, and the GEOM event machinery. GEOM is its own subsystem with its own conventions; Chapter 27 covers it.
- **`epoch(9)`-based concurrency**, which is the modern locking pattern for ifnet hot paths. Chapter 24 mentions it only in context. Chapter 28 (network drivers) returns to it alongside `iflib(9)` where epoch-style concurrency is needed in practice.
- **`mac(9)` (Mandatory Access Control) integration**, which adds policy hooks around the integration surfaces. The MAC framework is a specialised topic that does not yet apply to the simple `myfirst` driver.
- **`vfs(9)` integration**, which is what file systems do. A character driver does not interact with VFS at the layer of `vop_open` or `vop_read`; it interacts with `cdevsw` and `devfs`. The chapter is careful not to confuse the two.
- **Cross-driver interfaces through `kobj(9)` and custom interfaces declared with the `INTERFACE` build mechanism**. These are how the network and storage stacks define their internal contracts. They are mentioned in context in Section 7 but the deep treatment belongs in a later, more advanced chapter.
- **The new `netlink(9)` interface that recent FreeBSD kernels expose for some network management traffic**. Netlink is currently used by the routing subsystem rather than by individual device drivers, and the right place to teach it is alongside the network chapter.
- **Custom protocol modules through `pr_protocol_init`**, which is for new transport protocols rather than for device drivers.

Staying inside those lines keeps Chapter 24 a chapter about how a driver becomes part of the kernel, not a chapter about every kernel subsystem a driver may eventually touch.

### Estimated Time Investment

- **Reading only**: four to five hours. Chapter 24's ideas are mostly conceptual extensions of things the reader has already met. The new vocabulary (devfs, cdev, ioctl, sysctl, ifnet, CAM) is mostly familiar by name from earlier chapters; the chapter's job is to give each of them a concrete shape.
- **Reading plus typing the worked examples**: ten to twelve hours over two or three sessions. The driver evolves through three integration surfaces in turn (devfs, ioctl, sysctl), each with its own short stage. Each stage is short and self-contained; the testing is what takes the time, because the integration surfaces are best tested by writing a small user-space program that drives them.
- **Reading plus all labs and challenges**: fifteen to eighteen hours over three or four sessions. The labs include a clone-aware devfs experiment, a full ioctl roundtrip with `myfirstctl`, a sysctl-driven counter monitoring exercise, a cleanup-discipline lab that intentionally breaks teardown to expose the failure pattern, and a small ifnet-stub challenge for readers who want a preview of Chapter 28.

Sections 3 and 4 are the densest in terms of new vocabulary. The ioctl macros and the sysctl callback signatures are the only truly new APIs in the chapter; the rest is composition. If the macros feel opaque on a first pass, that is normal. Stop, run the matching exercise on the driver, and come back when the shape has settled.

### Prerequisites

Before starting this chapter, confirm:

- Your driver source matches Chapter 23 Stage 3 (`1.6-debug`). The starting point assumes every Chapter 23 primitive: the `DPRINTF` macro, the SDT probes, the debug mask sysctl, and the `myfirst_debug.c`/`myfirst_debug.h` file pair. Chapter 24 builds new integration code that uses each of those at appropriate moments.
- Your lab machine runs FreeBSD 14.3 with `/usr/src` on disk and matching the running kernel.
- A debug kernel with `INVARIANTS`, `WITNESS`, `WITNESS_SKIPSPIN`, `DDB`, `KDB`, `KDB_UNATTENDED`, `KDTRACE_HOOKS`, and `DDB_CTF` is built, installed, and booting cleanly.
- `bhyve(8)` or `qemu-system-x86_64` is available, and you have a usable VM snapshot at the `1.6-debug` state. Chapter 24's labs include intentional failure scenarios for the cleanup-discipline section, and a snapshot makes recovery cheap.
- The following user-space commands are in your path: `dmesg`, `sysctl`, `kldstat`, `kldload`, `kldunload`, `devctl`, `cc`, `make`, `dtrace`, `dd`, `head`, `cat`, `chown`, `chmod`, and `truss`. Chapter 24's labs make light use of `truss`, the FreeBSD equivalent of Linux `strace`, to verify that user-space programs really do reach the driver through the new ioctls.
- You are comfortable writing a short C program against the FreeBSD `libc` headers. The chapter introduces the user-space side of the new ioctls through a small program named `myfirstctl`.
- A working knowledge of `git` is helpful but not required. The chapter recommends you commit between stages so each version of the driver has a recoverable point.

If any item above is shaky, fix it now. Integration code is, on balance, less dangerous than the kernel-mode work of earlier chapters because most of the failure modes are caught at module load or at user-space invocation rather than producing kernel panics. But the lessons compound: a clone-aware `make_dev` mistake in this chapter will produce ugly diagnostics in Chapter 28 when the network driver also wants its own nodes, and a sysctl OID leak in this chapter will produce confusing module-load failures in Chapter 27 when the storage driver tries to register names that already exist.

### How to Get the Most Out of This Chapter

Five habits pay off in this chapter more than in any of the previous Part 5 chapters.

First, keep `/usr/src/sys/dev/null/null.c`, `/usr/src/sys/sys/conf.h`, `/usr/src/sys/sys/ioccom.h`, and `/usr/src/sys/sys/sysctl.h` bookmarked. The first is the shortest non-trivial character driver in the FreeBSD source tree and is the canonical example of the `cdevsw`/`make_dev`/`destroy_dev` pattern. The second declares the `cdevsw` structure, the `make_dev` family, and the `MAKEDEV_*` flag bits the chapter uses repeatedly. The third defines the ioctl encoding macros (`_IO`, `_IOR`, `_IOW`, `_IOWR`) and contains the `IOC_VOID`, `IOC_IN`, `IOC_OUT`, and `IOC_INOUT` constants the kernel uses to decide whether to copy data automatically. The fourth defines the sysctl OID macros, the `SYSCTL_HANDLER_ARGS` calling convention, and the static and dynamic OID interfaces. None of these files are long; the longest is a few thousand lines and most of that is comments. Reading each once at the start of the corresponding section is the single most effective thing you can do for fluency.

Second, keep three real driver examples close to hand: `/usr/src/sys/dev/null/null.c`, `/usr/src/sys/net/if_tuntap.c`, and `/usr/src/sys/dev/virtio/block/virtio_blk.c`. The first is the minimal cdevsw example. The second is the canonical clone-aware `dev_clone` example used by Section 5 to introduce ifnet. The third illustrates a full `device_get_sysctl_ctx` based dynamic sysctl tree and a `SYSCTL_ADD_PROC` callback that toggles a runtime knob. Chapter 24 points back to each at the right moment. Reading them once now, without trying to memorise, gives the rest of the chapter concrete anchors to hang its ideas on.

> **A note on line numbers.** Pointers into `null.c`, `if_tuntap.c`, and `virtio_blk.c` later in the chapter are anchored on named symbols: a specific `make_dev` call, a `SYSCTL_ADD_PROC` handler, a particular `cdevsw`. Those names carry across future FreeBSD 14.x releases. The specific line each name sits on does not. When the prose quotes a location, open the file and search for the symbol rather than scrolling to the number.

Third, type every code change into the `myfirst` driver by hand. Integration code is the kind of code that is easy to copy and very hard to remember later. Typing out the `cdevsw` table, the ioctl command definitions, the sysctl tree construction, and the user-space `myfirstctl` program builds the kind of familiarity that copy-paste cannot. The goal is not to have the code; the goal is to be the person who could write it again, from scratch, in twenty minutes when a future bug demands it.

Fourth, build the user-space program at every stage. Many of the chapter's lessons are visible only on the user side. Whether the kernel has copied an ioctl payload correctly, whether a sysctl is readable but not writable, whether a `/dev` node has the permissions you set, whether a clone produces a usable device node, all of those questions are answered with `cat`, `dd`, `chmod`, `sysctl`, `truss`, and the small `myfirstctl` companion program. A driver tested only by its kernel-side counters has been only half tested.

Fifth, after finishing Section 4 re-read Chapter 23's debug discipline. Each integration surface in Chapter 24 is wrapped in a `DPRINTF` from Chapter 23. Each ioctl path fires `MYF_DBG_IOCTL` log lines. Each sysctl path can be observed through the SDT machinery. Seeing how Chapter 23's tools serve Chapter 24's interfaces reinforces both chapters and prepares the reader for Chapter 25, where the same pattern continues.

### Roadmap Through the Chapter

The sections in order are:

1. **Why Integration Matters.** The conceptual story. From standalone module to system component; the price of leaving integration as an afterthought; the four user-visible interfaces every integrated driver eventually needs; the optional subsystem hooks the chapter introduces but does not finish.
2. **Working With devfs and the Device Tree.** The kernel's view of `/dev`. What devfs is and how it differs from the static device tables of older systems; the lifecycle of a `cdev`; `make_dev` and friends in detail; the `cdevsw` structure and its callbacks; the `si_drv1`/`si_drv2` fields; permissions and ownership; clonable nodes through the `dev_clone` event handler. Stage 1 of the Chapter 24 driver replaces the original ad-hoc node creation with a clean clone-aware pattern.
3. **Implementing `ioctl()` Support.** The user-driven control interface. What ioctls are; the `_IO`/`_IOR`/`_IOW`/`_IOWR` encoding; the kernel's automatic copyin/copyout layer; how to choose a magic letter and number; how to lay out a public ioctl header; how to write a `d_ioctl` callback that dispatches on the command word; common pitfalls (variable-length data, embedded pointers, version evolution). Stage 2 adds `myfirst_ioctl.c` and `myfirst_ioctl.h` and a small `myfirstctl` user-space program.
4. **Exposing Metrics Through `sysctl()`.** The monitoring and tuning interface. What sysctls are; static versus dynamic OIDs; the `device_get_sysctl_ctx`/`device_get_sysctl_tree` pattern; counters, knobs, and procedural callbacks; the `SYSCTL_ADD_*` family; tunables in `/boot/loader.conf`; access control and units. Stage 3 adds `myfirst_sysctl.c` and a per-instance metrics subtree.
5. **Integration With the Networking Subsystem (Optional).** A brief, conceptual look at ifnet. What `if_t` is; the `if_alloc`/`if_initname`/`if_attach`/`bpfattach`/`if_detach`/`if_free` outline; how `tun(4)` and `tap(4)` are structured around it; what role the driver plays inside the larger network stack. The section is short on purpose; Chapter 28 is the network driver chapter.
6. **Integration With the CAM Storage Subsystem (Optional).** A brief, conceptual look at CAM. What CAM is; what a SIM and a CCB are; the `cam_sim_alloc`/`xpt_bus_register`/`xpt_action`/`xpt_done` outline; how a small read-only memory disk could be exposed through it. The section is short on purpose; Chapter 27 is the storage driver chapter.
7. **Registration, Teardown, and Cleanup Discipline.** The cross-cutting topic. Module event handlers (`MOD_LOAD`, `MOD_UNLOAD`, `MOD_SHUTDOWN`); attach failure with partial cleanup; detach failure when users still hold open handles; the cleanup-on-failure pattern; ordering between integration surfaces; what `bus_generic_attach`, `bus_generic_detach`, and `device_delete_children` do for you; SYSINIT and EVENTHANDLER for cross-cutting registrations.
8. **Refactoring and Versioning an Integrated Driver.** The clean house. The final split into `myfirst.c`, `myfirst_debug.c`/`.h`, `myfirst_ioctl.c`/`.h`, and `myfirst_sysctl.c`/`.h`; the public `myfirst.h` header for user-space callers; the `INTEGRATION.md` document; the version bump to `1.7-integration`; the regression test additions; the commit and tag.

After the eight sections come a set of hands-on labs that exercise each integration surface end to end, a set of challenge exercises that stretch the reader without introducing new foundations, a troubleshooting reference for the symptoms most readers will hit, a Wrapping Up that closes Chapter 24's story and opens Chapter 25's, a bridge to the next chapter, and the usual quick-reference card and glossary.

If this is your first pass, read linearly and do the labs in order. If you are revisiting, Sections 2 and 3 stand alone and make good single-sitting reads. Section 5 and Section 6 are short and conceptual and can be skipped on a first pass without losing the chapter's main thread, then returned to before starting Chapter 26 or Chapter 27.

A small note before the technical work begins. The chapter often asks the reader to compile a tiny user-space program, run it against the driver, observe the result, and then return to the kernel side to read what happened. That rhythm is deliberate. Integration is not a property of the driver alone; it is a property of the relationship between the driver and the rest of the system. The user-space programs are short, but they are how the chapter measures whether each integration surface really works.

## Section 1: Why Integration Matters

Before the code, the framing. Section 1 spells out what changes when a driver becomes integrated. The reader who has followed Part 4 and the first chapters of Part 5 has built a working driver. What does it mean to say that this driver is not yet *integrated*, and what specific qualities does integration add?

This section answers that question carefully and at length, because the rest of the chapter is the implementation of these qualities. A reader who knows clearly *why* each integration surface exists will find the implementation work in Sections 2 through 8 much easier. A reader who skips the framing will spend the chapter wondering why the driver needs an `ioctl` at all when an internal sysctl could do the same job; that question has a real answer, and Section 1 is where the answer lives.

### From Standalone Module to System Component

A standalone driver is a kernel module that does its job correctly when called by the kernel and stays out of the way otherwise. The current `myfirst` driver, at version `1.6-debug`, is exactly that kind of module. It has a single `cdev` node, no public ioctls, no published sysctls beyond a handful of internal debug knobs, no relationship to any kernel subsystem outside its own little corner, and no expectation that any user-space program will reach in and tell it what to do. It works, and it works in isolation.

A system component, by contrast, is a kernel module whose value depends on its relationships with the rest of the system. The same `myfirst` driver, integrated, presents a `/dev/myfirst0` node with the right permissions for the role of its hardware, exposes a small ioctl interface that user programs can use to reset the device or query its status, publishes a per-instance sysctl tree that monitoring software can scrape, registers itself with the appropriate kernel subsystem if the hardware is a network or storage device, and cleans up cleanly when unloaded. Each of those interfaces is small. Together they are the difference between a driver that runs on one developer's lab machine and a driver that ships with FreeBSD.

The shift from standalone to integrated is not a one-line change to the driver. It is a series of conscious decisions, each of which expands the driver's public surface and each of which incurs a maintenance cost. Decisions that look small in the moment, like the choice of a magic letter for an ioctl or the name of a sysctl OID, become long-lived contracts. A driver that picked the letter `M` for its ioctls in 2010 still has those numbers in its public header today, because changing them would break every user-space program that ever called them.

A useful way to picture the shift is to imagine two readers approaching the driver. The first reader is the developer who wrote the driver: they know everything about it, they can change anything in it, and they can rebuild it whenever they like. The second reader is the system administrator who installs the module from a package and never reads its source: they only ever see the driver through its `/dev` nodes, its sysctls, its ioctls, and its log messages. A standalone driver is one designed for the first reader; an integrated driver is one designed for both.

Chapter 24 teaches the reader how to design for the second reader. That is the conceptual work the chapter does, and the code work in Sections 2 through 8 is the practical realisation of it.

### Common Integration Targets

A FreeBSD driver typically integrates with four kernel-side surfaces and, depending on the hardware, with one of two subsystems. Naming the targets clearly at the outset helps the reader hold the chapter's structure in mind.

The first target is **`devfs`**. Every character driver creates one or more `/dev` nodes through `make_dev` (or one of its variants) and removes them through `destroy_dev`. The shape and naming of those nodes is how the rest of the system addresses the driver. A node named `/dev/myfirst0` lets the administrator open it with `cat /dev/myfirst0`, lets a script include it in a `find /dev -name 'myfirst*'`, and lets the kernel itself dispatch open, read, write, and ioctl calls into the driver. Section 2 is dedicated to devfs.

The second target is **`ioctl(2)`**. The `read(2)` and `write(2)` syscalls move bytes; they do not control the driver. Any control operation that does not fit the read/write data-flow model lives in `ioctl(2)`. A user-space program calls `ioctl(fd, MYF_RESET)` to ask the driver to reset its hardware, or `ioctl(fd, MYF_GET_STATS, &stats)` to read out a counter snapshot. Each ioctl is a small, well-typed entry point with a number, a direction, and a payload. Section 3 is dedicated to ioctls.

The third target is **`sysctl(8)`**. Counters, statistics, and tunable parameters live in the system-wide sysctl tree, accessible from user space with `sysctl(8)` and from C with `sysctlbyname(3)`. A driver places its OIDs under `dev.<driver>.<unit>.<name>` so that `sysctl dev.myfirst.0` lists every metric and knob the device exposes. The sysctl interface is the right home for read-only counters and for slow-changing knobs; ioctl is the right home for fast actions and for typed data. Section 4 is dedicated to sysctls.

The fourth target is **the kernel's lifecycle hooks**. The module event handler (`MOD_LOAD`, `MOD_UNLOAD`, `MOD_SHUTDOWN`), the device tree's attach and detach methods, and the cross-cutting registrations through `EVENTHANDLER(9)` and `SYSINIT(9)` together define what happens when the driver enters and leaves the kernel. A driver that gets the integration interfaces right but the lifecycle wrong leaks resources, deadlocks at unload, or panics on the third `kldload`/`kldunload` cycle. Section 7 is dedicated to lifecycle discipline.

In addition, drivers whose hardware is a network device integrate with the **`ifnet`** subsystem, and drivers whose hardware is a storage device integrate with the **`CAM`** subsystem. Both are conceptually large enough that a deep treatment requires its own chapter (Chapter 28 for ifnet, Chapter 27 for CAM and GEOM). Section 5 and Section 6 of this chapter introduce them at the level needed to recognise their shape and to know what kind of work they involve.

Each target deserves naming for a specific reason. `devfs` is the name a casual user types into a shell. `ioctl` is the entry point for typed actions a program needs to issue against a device. `sysctl` is the place a monitoring tool looks for numbers. The lifecycle hooks are the place a packager and a system administrator hit when they `kldload` and `kldunload`. The four together cover the four faces of the driver that matter to anyone other than its author.

### Benefits of Proper Integration

Integration is not an end in itself. It is a means to a small set of practical outcomes that the chapter will keep returning to.

The first outcome is **monitoring**. A driver whose counters are visible through `sysctl` can be scraped by `prometheus-fbsd-exporter`, by Nagios checks, by a small shell script that runs every minute. A driver whose counters are only visible through `device_printf` to `dmesg` can only be inspected by reading the log file by hand. The two operational realities are very different, and the difference is determined entirely by the integration choice the developer made when the driver was written.

The second outcome is **management**. A driver that exposes an ioctl named `MYF_RESET` lets the administrator script a reset cycle into a maintenance window. A driver without that interface forces the administrator to `kldunload` and `kldload` the module, which is a much heavier operation that drops every open file descriptor and may not be acceptable while production traffic is flowing through the device.

The third outcome is **automation**. A driver that emits well-formed `/dev` nodes with predictable names lets `devd(8)` react to attach and detach events, run scripts on hotplug, and integrate the driver into the larger system's startup, shutdown, and recovery flows. A driver that emits an opaque single node and never tells anyone about its lifecycle cannot be automated without resorting to `dmesg` log scraping, which is brittle.

The fourth outcome is **reusability**. A driver whose ioctl interface is well documented can be the foundation of higher-level libraries. The `bsnmp` daemon, for example, uses well-defined kernel interfaces to expose driver counters through SNMP without touching the driver source. A driver that designed its interfaces correctly the first time gains those benefits without further work.

These four outcomes (monitoring, management, automation, reusability) are the practical reason for everything that follows. Each section in the chapter delivers a piece of one of these outcomes, and the closing refactor in Section 8 is what makes the whole package presentable to the rest of the system.

### A Small Tour of System Tools That Depend on Integration

A useful exercise before the technical work is to look at the user-space tools that exist precisely because drivers integrate with the kernel subsystems above. None of these tools work for a standalone driver. All of them work, automatically, for an integrated one.

`devinfo(8)` walks the kernel's device tree and prints what it finds. It works because every device in the tree was registered through the newbus interface and every device has a name, a unit number, and a parent. The administrator runs `devinfo -v` and sees the entire device hierarchy, including the `myfirst0` instance.

`sysctl(8)` reads from and writes to the kernel sysctl tree. It works because every counter and knob in the kernel is reachable through the OID hierarchy, including the OIDs the driver registered through `device_get_sysctl_tree`.

`devctl(8)` lets the administrator manipulate individual devices: `devctl detach`, `devctl attach`, `devctl suspend`, `devctl resume`, `devctl rescan`. It works because every device implements the kobj methods the kernel's device tree machinery expects. Chapter 22 already used `devctl suspend myfirst0` and `devctl resume myfirst0`.

`devd(8)` watches the kernel's device-event channel and runs scripts in response to attach, detach, hotplug, and similar events. It works because the kernel emits structured events for each newbus operation. A driver that follows the standard newbus pattern is automatically visible to `devd`.

`ifconfig(8)` configures network interfaces. It works because every network driver registers with the ifnet subsystem and accepts a standard set of ioctls (Section 5 introduces this).

`camcontrol(8)` controls SCSI and SATA devices through CAM. It works because every storage driver registers a SIM and processes CCBs (Section 6 introduces this).

`gstat(8)` shows real-time GEOM statistics, `geom(8)` lists the GEOM tree, `top -H` shows per-thread CPU usage. Each of these tools relies on a specific integration surface that drivers register with. The driver that ignores those surfaces gets none of the benefit.

A simple way to confirm the point is to run, on your lab machine, the following exercises:

```sh
# Tour the device tree
devinfo -v | head -40

# Tour the sysctl tree, just the dev branch
sysctl dev | head -40

# See what the live network interfaces look like
ifconfig -a

# See what storage looks like through CAM
camcontrol devlist

# See what GEOM sees
geom -t

# Watch the device event channel for a few seconds
sudo devd -d -f /dev/null &
DEVDPID=$!
sleep 5
kill $DEVDPID
```

Each command exists because drivers integrate. Read the output and notice how much of what is visible came from drivers that did the integration work. The `myfirst` driver, at the start of Chapter 24, contributes almost nothing to that output. By the end of Chapter 24, it will contribute its `dev.myfirst.0` subtree to `sysctl`, its `myfirst0` device to `devinfo -v`, and its `/dev/myfirst*` nodes to the file system. Each step is small. The aggregate is the difference between a one-off lab driver and a real piece of FreeBSD.

### What "Optional" Means in This Chapter

Section 5 (networking) and Section 6 (storage) are labelled optional. The label deserves a careful definition.

Optional does not mean unimportant. Both sections will become essential reading later in the book, the networking material before Chapter 27 and the storage material before Chapter 26 and Chapter 28. Optional means that for a reader who is following the `myfirst` PCI driver as the running example, the network and storage hooks would not be exercised in this chapter, because `myfirst` is not a network device and is not a storage device. The chapter introduces the conceptual shape of those hooks so that the reader recognises them when they appear, and so that the structural decisions in Section 7 and Section 8 take them into account.

A reader on a first pass who is short on time can skip Sections 5 and 6. The other sections do not depend on them. A reader who plans to follow Chapter 26 or Chapter 27 should read them, because they introduce the vocabulary those chapters will assume.

The chapter is honest about the depth of what it teaches in those sections. The networking subsystem is several thousand lines of source code in `/usr/src/sys/net`. The CAM subsystem is several thousand lines of source code in `/usr/src/sys/cam`. Each took years to design and is still evolving. Chapter 24 introduces them at the level of *here is the shape*, *here are the calls a driver typically makes*, *here is one real driver that uses each*. The full mechanics belong elsewhere.

### Pitfalls on the Way to Integration

Three pitfalls trip up most first-time integrators.

The first pitfall is **adding integration surfaces ad hoc**. A driver that grew an ioctl one Tuesday because the developer needed a quick way to test the device, and a sysctl the following week because the developer wanted to see a counter, and another ioctl the following month because of a reported bug, ends up with a public surface that is inconsistent in style, inconsistent in naming, inconsistent in error handling, and inconsistent in documentation. The right pattern is to design the public surface deliberately, to use consistent naming, and to document each entry point in a header file before writing the implementation. Section 3 and Section 8 return to this discipline.

The second pitfall is **mixing concerns inside the device methods**. A driver whose `device_attach` does the kobj work, the resource allocation, the devfs node creation, the sysctl tree construction, and the user-space-facing setup in one long function quickly becomes unreadable. The chapter recommends separating these concerns into helper functions at first, and into separate source files in Section 8. The `myfirst_debug.c` and `myfirst_debug.h` pair from Chapter 23 was the first step in that direction; the new `myfirst_ioctl.c` and `myfirst_sysctl.c` files from this chapter continue the pattern.

The third pitfall is **not testing the public surface from user space**. A driver that the developer tested only by exercising it from the kernel side will pass every kernel-side test and still fail the moment a real user-space program calls it, because the developer assumed something about the calling convention that does not hold in practice. The chapter therefore insists on building the small `myfirstctl` companion program as soon as the driver has any ioctls, and on testing every sysctl through `sysctl(8)` rather than only by reading the in-driver counter directly. The user-space tests are the only ones that confirm integration actually works.

These pitfalls are not unique to FreeBSD. They show up in every operating system that has a kernel-user boundary. FreeBSD's tooling makes it easier than most systems to do the right thing, because the conventions are well documented in the manual pages (`devfs(5)`, `ioctl(2)`, `sysctl(9)`, `style(9)`) and because the kernel itself ships with hundreds of integrated drivers the reader can study. The chapter relies on those conventions and points back to the matching real drivers as it goes.

### A Mental Model for the Chapter

Before moving into Section 2, it helps to fix a single picture in mind. The driver, by the end of Chapter 24, will look like this from the outside:

```text
Userland tools                          Kernel
+----------------------+                +----------------------+
| myfirstctl           |  ioctl(2)      | d_ioctl callback     |
| sysctl(8)            +--------------->| sysctl OID tree      |
| cat /dev/myfirst0    |  read/write    | d_read, d_write      |
| chmod, chown         |  fileops       | devfs node lifecycle |
| devinfo -v           |  newbus query  | device_t myfirst0    |
| dtrace -n 'myfirst:::'|  SDT probes   | sc_debug, DPRINTF    |
+----------------------+                +----------------------+
```

Every entry on the left is a tool a real FreeBSD user already knows. Every entry on the right is a piece of integration the chapter teaches you to write. The arrows in between are what each section of the chapter implements.

Hold that picture in mind as the technical work begins. The point of every section that follows is to add one of those arrows to the driver, with the discipline that lets the arrow stay reliable as the driver grows.

### Wrapping Up Section 1

Integration is the discipline of making a driver visible, controllable, and observable from outside its own source code. The four primary integration surfaces in FreeBSD are devfs, ioctl, sysctl, and the kernel lifecycle hooks. Two optional subsystem hooks are ifnet for network devices and CAM for storage devices. Together, these are how a driver stops being a one-developer project and becomes a piece of FreeBSD. The remaining sections of this chapter implement each surface, with the `myfirst` driver as the running example and the version bumping from `1.6-debug` to `1.7-integration` as the visible milestone.

In the next section, we turn to the first and most foundational integration surface: `devfs` and the device file system that gives every character driver its `/dev` presence.

## Section 2: Working With devfs and the Device Tree

The first integration surface every character driver crosses is `devfs`, the device file system. The reader has been creating `/dev/myfirst0` since the earliest chapters, but the call to `make_dev` was always presented as a single line of boilerplate without much explanation. Section 2 fills that gap. It explains what devfs actually is, walks through the lifecycle of a `cdev`, surveys the variants of `make_dev` and the `cdevsw` callback table in detail, shows how to attach per-node state through `si_drv1` and `si_drv2`, teaches the modern clone-aware pattern for drivers that want one node per instance on demand, and ends by showing how an administrator can adjust permissions and ownership without rebuilding the driver.

### What devfs Is

`devfs` is a virtual file system that exposes the kernel's set of registered character devices as a tree of files under `/dev`. It is virtual in the same sense `procfs` and `tmpfs` are virtual: there is no on-disk storage backing it. Every file under `/dev` is the kernel's projection of a `cdev` structure into the file system namespace. When a user-space program calls `open("/dev/null", O_RDWR)`, the kernel looks up the path in devfs, finds the matching `cdev`, follows the pointer to the `cdevsw` table, and dispatches the open through it.

Older UNIX systems used a static device tree. An administrator ran a program like `MAKEDEV` or edited `/dev` directly, and the file system contained device nodes whether the corresponding hardware was present or not. The static approach had two well-known problems. First, the administrator had to know in advance which devices were possible and create the matching nodes by hand, with the right major and minor numbers. Second, the file system contained orphan nodes for hardware the system did not actually have, which was confusing.

FreeBSD's `devfs`, introduced as the default in the early 5.x release series, replaced the static scheme with a dynamic one. The kernel itself decides which device nodes exist, based on which drivers have called `make_dev`. When the driver calls `make_dev`, the node appears under `/dev`. When the driver calls `destroy_dev`, the node disappears. The administrator does not maintain device entries by hand any more, and there are no orphan nodes for hardware that is not present.

The trade-off devfs introduces is that the lifecycle of a `/dev` node is now controlled by the kernel rather than by the file system. A driver that fails to remove its node on detach leaves it visible until the kernel itself removes it (which it eventually will, but not as immediately as the driver). A driver that accidentally creates the same node twice gets a panic from the duplicate-name check. A driver that creates a node from the wrong context can deadlock the kernel. The chapter teaches the patterns that avoid each of these.

A useful detail for understanding devfs is that the kernel keeps a single global namespace of device nodes, and `make_dev` registers into that namespace. The administrator can mount additional devfs instances inside jails or chroots; each instance projects a filtered view of the global namespace, controlled through `devfs.rules(8)`. The driver itself does not need to know about these projections. It simply registers its `cdev` once, and the kernel and the rule system together decide which views can see it.

### The Lifecycle of a `cdev`

Every `cdev` goes through five stages. Knowing these stages by name makes the rest of the section easier to follow.

The first stage is **registration**. The driver calls `make_dev` (or one of its variants) from `device_attach` (or from a module event handler, depending on whether the device is bus-attached or pseudo-device). The call returns a `struct cdev *` that the driver stores in its softc. From this moment on, the node is visible under `/dev`.

The second stage is **use**. User-space programs can open the node and call read, write, ioctl, mmap, poll, or kqueue against it. Each call goes through the matching cdevsw callback the driver installed at registration time. A `cdev` may have many open file handles against it at any moment, and the driver's callbacks must be safe to call concurrently with each other and with the driver's own internal work (interrupts, callouts, taskqueues).

The third stage is **destruction request**. The driver calls `destroy_dev` (or `destroy_dev_sched` for the asynchronous variant). The node is unlinked from `/dev` immediately, so no new opens can succeed. Existing opens are not closed at this moment.

The fourth stage is **drain**. `destroy_dev` blocks until every open file handle for the cdev has gone through `d_close` and every in-flight call into the driver has returned. The driver's callbacks are guaranteed not to be called any more once `destroy_dev` returns.

The fifth stage is **release**. Once `destroy_dev` returns, the driver can free the softc, release any resources the cdev callbacks were using, and unload the module. The `struct cdev *` itself is owned by the kernel and is freed by the kernel when its last reference goes away; the driver does not free it.

The drain step is the one that catches first-time drivers most often. A naive driver does the equivalent of "destroy_dev; free(sc);" and then a held open file handle calls into the cdevsw and dereferences the freed softc, which panics. The chapter teaches how to handle this correctly: put the destroy_dev call before any state-freeing in the detach path, and trust the kernel to drain the in-flight calls before the destroy returns.

### The `make_dev` Family

FreeBSD provides several variants of `make_dev`, each with a different combination of options. They live in `/usr/src/sys/sys/conf.h` and `/usr/src/sys/fs/devfs/devfs_devs.c`. The chapter introduces the most useful four.

The simplest form is **`make_dev`** itself:

```c
struct cdev *
make_dev(struct cdevsw *devsw, int unit, uid_t uid, gid_t gid,
    int perms, const char *fmt, ...);
```

This call creates a node owned by `uid:gid` with permissions `perms`, named according to the `printf`-style format. It uses `M_WAITOK` internally and may sleep, so it must be called from a context that can sleep (typically `device_attach` or a module load handler, never an interrupt handler). It cannot fail: if it cannot allocate memory, it sleeps until it can. The reader has been using this form since the early chapters.

The richer form is **`make_dev_credf`**:

```c
struct cdev *
make_dev_credf(int flags, struct cdevsw *devsw, int unit,
    struct ucred *cr, uid_t uid, gid_t gid, int mode,
    const char *fmt, ...);
```

This variant takes an explicit `flags` argument and an explicit credential. The credential is used by the MAC framework when checking whether the device may be created with the given owner. The flags select features such as `MAKEDEV_ETERNAL` (the kernel never destroys this node automatically) and `MAKEDEV_ETERNAL_KLD` (the same, but allowed only inside a loadable module). The `null(4)` driver uses this form, as the chapter quoted in Section 1's reference list.

The form recommended for new drivers is **`make_dev_s`**:

```c
int
make_dev_s(struct make_dev_args *args, struct cdev **cdev,
    const char *fmt, ...);
```

This variant takes a structure of arguments rather than a long argument list. The structure is initialised with `make_dev_args_init(&args)` before its fields are filled in. The advantage of `make_dev_s` is that it can fail rather than sleep, and the failure is reported through a return value rather than through a sleep. It also has an output parameter for the `cdev *`, which means the caller does not need to remember which positional return represents what. New code should prefer `make_dev_s` because the failure path is cleaner.

The argument structure looks like this:

```c
struct make_dev_args {
    size_t        mda_size;
    int           mda_flags;
    struct cdevsw *mda_devsw;
    struct ucred  *mda_cr;
    uid_t         mda_uid;
    gid_t         mda_gid;
    int           mda_mode;
    int           mda_unit;
    void          *mda_si_drv1;
    void          *mda_si_drv2;
};
```

The `mda_size` field is used by the kernel to detect ABI mismatches; `make_dev_args_init` sets it correctly. The `mda_si_drv1` and `mda_si_drv2` fields let the driver attach two pointers of its own to the cdev at creation time; the chapter uses `mda_si_drv1` to attach a pointer to the softc.

The `MAKEDEV_*` flag bits relevant to most drivers are:

| Flag                    | Meaning                                                          |
|-------------------------|------------------------------------------------------------------|
| `MAKEDEV_REF`           | The returned cdev is referenced; balance with `dev_rel`.         |
| `MAKEDEV_NOWAIT`        | Do not sleep; return failure if the call would have to sleep.    |
| `MAKEDEV_WAITOK`        | The call may sleep (default for `make_dev`).                     |
| `MAKEDEV_ETERNAL`       | The kernel does not destroy this node automatically.             |
| `MAKEDEV_ETERNAL_KLD`   | Same as ETERNAL but allowed inside a loadable module.            |
| `MAKEDEV_CHECKNAME`     | Validate the name against the devfs character set.                |

The `make_dev_p` variant is similar to `make_dev_s` but takes a positional argument list. It is older, still supported, and used by some drivers in the tree; new drivers can ignore it in favour of `make_dev_s`.

### The `cdevsw` Structure

The `cdevsw` table is the dispatch table for character device callbacks. The reader has installed one in earlier chapters, but Section 2 examines it field by field.

A minimal modern cdevsw looks like:

```c
static struct cdevsw myfirst_cdevsw = {
    .d_version = D_VERSION,
    .d_flags   = D_TRACKCLOSE,
    .d_name    = "myfirst",
    .d_open    = myfirst_open,
    .d_close   = myfirst_close,
    .d_read    = myfirst_read,
    .d_write   = myfirst_write,
    .d_ioctl   = myfirst_ioctl,
};
```

`d_version` must be set to `D_VERSION`. The kernel uses this field to detect drivers built against an older cdevsw layout. A missing or wrong `d_version` is a common source of confusing module-load failures; always set it explicitly.

`d_flags` controls a small set of optional behaviours. The most common flags are:

| Flag             | Meaning                                                                |
|------------------|------------------------------------------------------------------------|
| `D_TRACKCLOSE`   | Call `d_close` on the last close of each fd, not on every close.        |
| `D_NEEDGIANT`    | Take the kernel-wide Giant lock around dispatch (rare in modern code).  |
| `D_NEEDMINOR`    | Allocate a minor number (legacy; rarely needed today).                  |
| `D_MMAP_ANON`    | The driver supports anonymous `mmap` through `dev_pager`.               |
| `D_DISK`         | The cdev is the entry point for a disk-like device.                     |
| `D_TTY`          | The cdev is a terminal device; affects line-discipline routing.         |

For the `myfirst` driver, `D_TRACKCLOSE` is the only flag worth setting. It causes the kernel to call `d_close` exactly once per file descriptor, on the last close of that descriptor, rather than on every close. Without `D_TRACKCLOSE`, a driver that wants to count open file handles has to handle the same `d_close` being called many times, which is awkward.

`d_name` is the name the kernel uses in some diagnostic messages. It is conventionally the same as the driver's name.

The callback fields are pointers to the functions that implement each operation. A driver only needs to install the callbacks it actually supports; missing callbacks default to safe stubs that return `ENODEV` or `EOPNOTSUPP`. The most common combination for a character driver is `d_open`, `d_close`, `d_read`, `d_write`, and `d_ioctl`. Drivers that support polling add `d_poll`. Drivers that support kqueue add `d_kqfilter`. Drivers that map memory into user space add `d_mmap` or `d_mmap_single`. Drivers that emulate a disk add `d_strategy`.

The `d_purge` callback is rare but worth knowing. The kernel calls it when the cdev is being destroyed and the driver should release any pending I/O. Most drivers do not need it because their `d_close` already handles release.

The `d_open` callback signature is:

```c
int myfirst_open(struct cdev *dev, int oflags, int devtype, struct thread *td);
```

`dev` is the cdev being opened. `oflags` is the union of the open(2) flags (`O_RDWR`, `O_NONBLOCK`, etc.). `devtype` carries the device type and is rarely useful for a character driver. `td` is the thread doing the open. The callback returns `0` on success or an errno on failure. A typical pattern stores the softc pointer found through `dev->si_drv1` into a per-open private structure that subsequent calls can recover.

The `d_close` signature is parallel:

```c
int myfirst_close(struct cdev *dev, int fflags, int devtype, struct thread *td);
```

The `d_read` and `d_write` signatures use the `uio` machinery the reader met in Chapter 8:

```c
int myfirst_read(struct cdev *dev, struct uio *uio, int ioflag);
int myfirst_write(struct cdev *dev, struct uio *uio, int ioflag);
```

The `d_ioctl` signature is:

```c
int myfirst_ioctl(struct cdev *dev, u_long cmd, caddr_t data,
    int fflag, struct thread *td);
```

`cmd` is the ioctl command word. `data` points to the ioctl data buffer (which the kernel has already copied in for `IOC_IN` commands and will copy out for `IOC_OUT` commands, as Section 3 will discuss in detail). `fflag` is the file flags from the open call. `td` is the calling thread.

### Per-Cdev State Through `si_drv1`

The cdev is the kernel's handle to the device, and the driver almost always needs a way to find its own softc from a cdev pointer. The standard mechanism is `cdev->si_drv1`. The driver sets this field when it creates the cdev (either at the call to `make_dev` or by writing to the field afterward) and reads it in every cdevsw callback.

The pattern looks like this in attach:

```c
sc->sc_cdev = make_dev(&myfirst_cdevsw, device_get_unit(dev),
    UID_ROOT, GID_WHEEL, 0660, "myfirst%d", device_get_unit(dev));
sc->sc_cdev->si_drv1 = sc;
```

And like this in each callback:

```c
static int
myfirst_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
    struct myfirst_softc *sc = dev->si_drv1;

    DPRINTF(sc, MYF_DBG_OPEN, "open: pid=%d flags=%#x\n",
        td->td_proc->p_pid, oflags);
    /* ... rest of open ... */
    return (0);
}
```

`si_drv2` is a second pointer the driver can use however it likes. Some drivers use it for a per-instance cookie; others use it for nothing and leave it `NULL`. The `myfirst` driver uses only `si_drv1`.

The `make_dev_s` variant is cleaner because it sets `si_drv1` at creation time:

```c
struct make_dev_args args;
make_dev_args_init(&args);
args.mda_devsw = &myfirst_cdevsw;
args.mda_uid = UID_ROOT;
args.mda_gid = GID_WHEEL;
args.mda_mode = 0660;
args.mda_si_drv1 = sc;
args.mda_unit = device_get_unit(dev);
error = make_dev_s(&args, &sc->sc_cdev, "myfirst%d", device_get_unit(dev));
if (error != 0) {
    device_printf(dev, "make_dev_s failed: %d\n", error);
    goto fail;
}
```

The `make_dev_s` form has the additional advantage that `si_drv1` is set before the cdev is visible in `/dev`, which closes a small but real race window in which a quick-to-open program could call into a cdevsw whose `si_drv1` was still `NULL`. New drivers should prefer this form.

### The `null(4)` Reference

The cleanest small example of cdevsw and `make_dev_credf` in the tree is `/usr/src/sys/dev/null/null.c`. The relevant excerpts are short enough to read in one pass. The cdevsw declarations (a separate one for `/dev/null` and `/dev/zero`):

```c
static struct cdevsw null_cdevsw = {
    .d_version = D_VERSION,
    .d_read    = (d_read_t *)nullop,
    .d_write   = null_write,
    .d_ioctl   = null_ioctl,
    .d_name    = "null",
};
```

The module event handler that creates and destroys the nodes:

```c
static int
null_modevent(module_t mod, int type, void *data)
{
    switch (type) {
    case MOD_LOAD:
        full_dev = make_dev_credf(MAKEDEV_ETERNAL_KLD, &full_cdevsw, 0,
            NULL, UID_ROOT, GID_WHEEL, 0666, "full");
        null_dev = make_dev_credf(MAKEDEV_ETERNAL_KLD, &null_cdevsw, 0,
            NULL, UID_ROOT, GID_WHEEL, 0666, "null");
        zero_dev = make_dev_credf(MAKEDEV_ETERNAL_KLD, &zero_cdevsw, 0,
            NULL, UID_ROOT, GID_WHEEL, 0666, "zero");
        break;
    case MOD_UNLOAD:
        destroy_dev(full_dev);
        destroy_dev(null_dev);
        destroy_dev(zero_dev);
        break;
    case MOD_SHUTDOWN:
        break;
    default:
        return (EOPNOTSUPP);
    }
    return (0);
}

DEV_MODULE(null, null_modevent, NULL);
MODULE_VERSION(null, 1);
```

A few details are worth pausing on. The `MAKEDEV_ETERNAL_KLD` flag tells the kernel that even if the module is unloaded under unusual circumstances, the cdev should not be silently invalidated. The `0666` mode means everyone can read and write; that is correct for `/dev/null`. The unit number is `0` because there is only ever one of each. The `MOD_LOAD` arm runs at module load and creates the nodes; the `MOD_UNLOAD` arm runs at module unload and destroys them; `MOD_SHUTDOWN` runs during the system shutdown sequence and does nothing here, because no shutdown work is needed for these pseudo-devices.

This is the canonical shape of a pseudo-device that lives at module scope rather than at device-tree scope. The `myfirst` driver, by contrast, creates its cdev in `device_attach` because the cdev's lifetime is tied to a specific PCI device's lifetime, not to the module's lifetime. The two patterns are different but coexist comfortably.

### Multiple Nodes and Clonable Nodes

A single driver can create more than one node. Three patterns are common.

The first is **fixed-name nodes**. The driver knows in advance how many nodes it needs and creates them with fixed names: `/dev/myfirst0`, `/dev/myfirst-status`, `/dev/myfirst-config`. This is the right pattern when each node has a different role and the count is known.

The second is **indexed nodes**. The driver creates one node per unit, named `/dev/myfirstN` where `N` is the unit number. This is the right pattern when each node represents a separate instance of the same kind of object, such as one per attached PCI card.

The third is **clonable nodes**. The driver registers a clone handler that creates a new node on demand whenever a user opens a name matching a pattern. The reader of `tun(4)` opens `/dev/tun` and gets `/dev/tun0`; opening `/dev/tun` again yields `/dev/tun1`; the kernel allocates the next free unit on each open. This is the right pattern for pseudo-devices that the user wants to "create" by simply opening them.

The clone mechanism is the `dev_clone` event handler. It lives in `/usr/src/sys/sys/conf.h`:

```c
typedef void (*dev_clone_fn)(void *arg, struct ucred *cred, char *name,
    int namelen, struct cdev **result);

EVENTHANDLER_DECLARE(dev_clone, dev_clone_fn);
```

The driver registers a handler with `EVENTHANDLER_REGISTER`, and the kernel calls the handler whenever an open path under `/dev` does not match an existing node. The handler decides whether the name belongs to its driver, allocates a new unit if so, calls `make_dev` to create the node with that name, and stores the resulting cdev pointer through the `result` argument. The kernel then re-opens the freshly created node and continues with the user's open call.

The `tun(4)` driver shows this pattern. From `/usr/src/sys/net/if_tuntap.c`:

```c
static eventhandler_tag clone_tag;

static int
tuntapmodevent(module_t mod, int type, void *data)
{
    switch (type) {
    case MOD_LOAD:
        clone_tag = EVENTHANDLER_REGISTER(dev_clone, tunclone, 0, 1000);
        if (clone_tag == NULL)
            return (ENOMEM);
        ...
        break;
    case MOD_UNLOAD:
        EVENTHANDLER_DEREGISTER(dev_clone, clone_tag);
        ...
    }
}

static void
tunclone(void *arg, struct ucred *cred, char *name, int namelen,
    struct cdev **dev)
{
    /* If *dev != NULL, another handler already created the cdev. */
    if (*dev != NULL)
        return;

    /* Examine name; if it matches our pattern, allocate a unit */
    /* and call make_dev to populate *dev. */
    ...
}
```

A few rules apply to clone handlers. The handler must not assume it is the only handler registered; multiple subsystems can register against `dev_clone`, and each handler must check whether `*dev` is already non-NULL before doing any work. The handler runs in a context that can sleep, so it can call `make_dev` directly. The handler should validate the name carefully because it is supplied by user space.

The `myfirst` driver uses an indexed-node pattern at first (one node per attached PCI device, `/dev/myfirst0`, `/dev/myfirst1`, etc.), and Section 2's lab walks through adding a clone handler so the user can open `/dev/myfirst-clone` and get a new unit on demand. The clone pattern is most useful for pseudo-devices that have no underlying hardware; for hardware-backed drivers it is rarely needed.

### Permissions, Ownership, and `devfs.rules`

`make_dev` takes the initial owner UID, group GID, and permissions for the node. These values are baked in at creation time. They are visible from user space through `ls -l /dev/myfirst0` and they determine which user-space programs can open the node.

For a hardware-backed driver, the right defaults depend on the role. A device that should only be accessible to root uses `UID_ROOT, GID_WHEEL, 0600`. A device that any administrative user should be able to access uses `UID_ROOT, GID_OPERATOR, 0660`. A device that any user can read but only root can write uses `UID_ROOT, GID_WHEEL, 0644`. The `myfirst` driver uses `UID_ROOT, GID_WHEEL, 0660` by default; the user is expected to be root or to be given access through `devfs.rules` if they need it.

The administrator can override these defaults at runtime through `devfs.rules(8)`. A typical rule file looks like:

```text
[localrules=10]
add path 'myfirst*' mode 0660
add path 'myfirst*' group operator
```

The administrator activates the rules by adding to `/etc/rc.conf`:

```text
devfs_system_ruleset="localrules"
```

After `service devfs restart` (or a reboot), the rules apply to new device nodes that appear. This mechanism lets the administrator grant access to the driver without rebuilding the module, which is the right division of responsibility: the developer chooses safe defaults, and the administrator widens them when needed.

A common mistake is to make a device world-writable by default because "it makes testing easier". A driver that ships with `0666` permissions to a device that controls hardware is a security problem. The chapter's recommendation is `0660` with `GID_WHEEL` as the default, and to instruct readers in `INTEGRATION.md` how to use `devfs.rules` to change it if they need to.

### Putting It All Together: The Stage 1 Driver

The Chapter 24 Stage 1 driver replaces the original ad-hoc node creation with the modern `make_dev_s` pattern, sets `si_drv1` at creation time, uses the `D_TRACKCLOSE` flag, and prepares the way for the ioctl callbacks to be added in Section 3. Here is the relevant excerpt of the new attach function. Type this in by hand; the whole point of the chapter is the careful change from the old ad-hoc form to the new disciplined form.

```c
static int
myfirst_attach(device_t dev)
{
    struct myfirst_softc *sc;
    struct make_dev_args args;
    int error;

    sc = device_get_softc(dev);
    sc->sc_dev = dev;

    /* ... earlier attach work: PCI resources, MSI-X, DMA, sysctl tree
     * stub, debug subtree.  See Chapters 18-23 for these.  ... */

    /* Build the cdev for /dev/myfirstN. */
    make_dev_args_init(&args);
    args.mda_devsw = &myfirst_cdevsw;
    args.mda_uid = UID_ROOT;
    args.mda_gid = GID_WHEEL;
    args.mda_mode = 0660;
    args.mda_si_drv1 = sc;
    args.mda_unit = device_get_unit(dev);
    error = make_dev_s(&args, &sc->sc_cdev, "myfirst%d",
        device_get_unit(dev));
    if (error != 0) {
        device_printf(dev, "make_dev_s failed: %d\n", error);
        DPRINTF(sc, MYF_DBG_INIT, "cdev creation failed (%d)\n", error);
        goto fail;
    }
    DPRINTF(sc, MYF_DBG_INIT, "cdev created at /dev/myfirst%d\n",
        device_get_unit(dev));

    /* ... rest of attach: register callouts, finalise sysctl OIDs, etc. */

    return (0);

fail:
    /* Unwind earlier resources in reverse order. */
    /* See Section 7 for the discipline that goes here. */
    return (error);
}
```

The corresponding cdevsw with `D_TRACKCLOSE`:

```c
static d_open_t myfirst_open;
static d_close_t myfirst_close;
static d_read_t myfirst_read;
static d_write_t myfirst_write;
static d_ioctl_t myfirst_ioctl;

static struct cdevsw myfirst_cdevsw = {
    .d_version = D_VERSION,
    .d_flags   = D_TRACKCLOSE,
    .d_name    = "myfirst",
    .d_open    = myfirst_open,
    .d_close   = myfirst_close,
    .d_read    = myfirst_read,
    .d_write   = myfirst_write,
    .d_ioctl   = myfirst_ioctl,
};
```

The corresponding open and close, with `si_drv1` lookups and per-open counter updates:

```c
static int
myfirst_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
    struct myfirst_softc *sc = dev->si_drv1;

    SDT_PROBE2(myfirst, , , open, sc, oflags);

    mtx_lock(&sc->sc_mtx);
    sc->sc_open_count++;
    mtx_unlock(&sc->sc_mtx);

    DPRINTF(sc, MYF_DBG_OPEN,
        "open: pid=%d flags=%#x open_count=%u\n",
        td->td_proc->p_pid, oflags, sc->sc_open_count);
    return (0);
}

static int
myfirst_close(struct cdev *dev, int fflags, int devtype, struct thread *td)
{
    struct myfirst_softc *sc = dev->si_drv1;

    SDT_PROBE2(myfirst, , , close, sc, fflags);

    mtx_lock(&sc->sc_mtx);
    KASSERT(sc->sc_open_count > 0,
        ("myfirst_close: open_count underflow"));
    sc->sc_open_count--;
    mtx_unlock(&sc->sc_mtx);

    DPRINTF(sc, MYF_DBG_OPEN,
        "close: pid=%d flags=%#x open_count=%u\n",
        td->td_proc->p_pid, fflags, sc->sc_open_count);
    return (0);
}
```

And the matching detach update, with `destroy_dev` before any softc state release:

```c
static int
myfirst_detach(device_t dev)
{
    struct myfirst_softc *sc = device_get_softc(dev);

    /* Refuse detach while users still hold the device open. */
    mtx_lock(&sc->sc_mtx);
    if (sc->sc_open_count > 0) {
        mtx_unlock(&sc->sc_mtx);
        device_printf(dev, "detach refused: %u open(s) outstanding\n",
            sc->sc_open_count);
        return (EBUSY);
    }
    mtx_unlock(&sc->sc_mtx);

    /* Destroy the cdev first.  The kernel drains any in-flight
     * callbacks before destroy_dev returns. */
    if (sc->sc_cdev != NULL) {
        destroy_dev(sc->sc_cdev);
        sc->sc_cdev = NULL;
        DPRINTF(sc, MYF_DBG_INIT, "cdev destroyed\n");
    }

    /* ... rest of detach: tear down DMA, MSI-X, callouts, sysctl ctx,
     * etc., in reverse order of attach.  See Section 7. */

    return (0);
}
```

This is the Stage 1 milestone. The driver now has a clean, modern, beginner-friendly devfs entry. The next stages will add ioctls and sysctls on top of this foundation.

### A Concrete Walkthrough: Loading and Inspecting the Stage 1 Driver

Build, load, and inspect the Stage 1 driver:

```sh
cd ~/myfirst-1.7-integration/stage1-devfs
make
sudo kldload ./myfirst.ko

# Confirm the device exists and has the expected attributes.
ls -l /dev/myfirst0

# Read its sysctl debug subtree (still from Chapter 23).
sysctl dev.myfirst.0

# Open it with cat to confirm the cdevsw read and close paths fire.
sudo cat /dev/myfirst0 > /dev/null
dmesg | tail -20
```

The expected output for `ls -l /dev/myfirst0`:

```text
crw-rw----  1 root  wheel  0x71 Apr 19 16:30 /dev/myfirst0
```

The `crw-rw----` says character device, owner read+write, group read+write, no permission for others. The owner is root. The group is wheel. The minor number is `0x71` (the kernel's allocation; the value will differ between systems). The size is the unit number that `make_dev_s` was called with.

The expected output for the `dmesg` snippet:

```text
myfirst0: cdev created at /dev/myfirst0
myfirst0: open: pid=4321 flags=0x1 open_count=1
myfirst0: close: pid=4321 flags=0x1 open_count=0
```

(Assuming the debug mask has `MYF_DBG_INIT` and `MYF_DBG_OPEN` set. If the mask is zero, the lines are silent; turn them on with `sysctl dev.myfirst.0.debug.mask=0xFFFFFFFF` from Chapter 23's debug discipline.)

If the open and close lines do not appear, check the debug mask. If the open line appears but not the close, you forgot `D_TRACKCLOSE` and the kernel is calling close on every fd close rather than only the last; either turn `D_TRACKCLOSE` on or expect multiple close lines per open. If the open count goes negative, you have a real bug: a close was called without a matching open.

### Common Mistakes With devfs

Five mistakes account for most of the devfs problems first-time integrators encounter. Naming them up front saves a debugging session later.

The first mistake is **calling `make_dev` from an unsleepable context**. `make_dev` may sleep. If it is called from an interrupt handler, from a callout, from inside a spin lock, or from inside any context where sleeping is forbidden, the kernel will panic with `WITNESS` or with `INVARIANTS` complaining about a sleepable function in a non-sleepable context. The fix is either to call `make_dev` from `device_attach` or from a module event handler (both safe contexts), or to use `make_dev_s` with `MAKEDEV_NOWAIT` (which then can fail and the caller must handle the failure).

The second mistake is **forgetting to set `si_drv1`**. The cdevsw callbacks then dereference a NULL pointer and panic the kernel. The fix is to set `si_drv1` immediately after `make_dev` (or, better, to use `make_dev_s` and set `mda_si_drv1` in the args, which closes the race window between creation and assignment).

The third mistake is **calling `destroy_dev` after freeing the softc**. The cdevsw callbacks may still be in flight when `destroy_dev` is called; the kernel drains them before `destroy_dev` returns. If the softc is already freed, the callbacks dereference garbage. The fix is to call `destroy_dev` first, then free the softc, in that strict order.

The fourth mistake is **creating two cdevs with the same name**. The kernel checks for duplicate names and the second `make_dev` call panics or returns an error depending on the variant. The fix is to compose the name from the unit number, or to use a clone handler.

The fifth mistake is **not handling the case where the device is open at detach**. A naive driver simply calls `destroy_dev` and frees the softc, which works only if no user has the device open. The user holding `/dev/myfirst0` open with `cat` defeats this. The fix is to refuse detach when `open_count > 0`, or to use the kernel's `dev_ref`/`dev_rel` machinery to coordinate. The chapter's pattern is the simple refusal (`return (EBUSY)`) because it gives the cleanest user-visible error.

### Pitfalls Unique to Multi-Instance Drivers

Drivers that create more than one cdev per attached device, or one cdev per channel inside a multi-channel device, hit a few additional pitfalls.

The first is **leaking nodes on partial failure**. If the driver creates three cdevs and the third call fails, it must destroy the first two before returning. Section 7's cleanup-on-failure pattern is the canonical solution. The chapter's lab in this section walks through the pattern with a deliberate failure injection.

The second is **forgetting that each cdev needs its own `si_drv1`**. A driver that creates per-channel nodes typically wants `si_drv1` to point to the channel rather than to the softc. The cdevsw callbacks then walk back from the channel to the softc as needed. Mixing the two leads to channels stomping on each other's state.

The third is **race-free node visibility**. Between `make_dev` (or `make_dev_s`) returning and the driver finishing the rest of attach, a fast user can already open the node. The driver must be ready to handle opens that arrive before attach is fully done. The simplest pattern is to defer `make_dev` to the very end of attach, so the node is only visible after every other piece of state is ready. The `myfirst` driver follows this pattern.

These pitfalls do not arise often for single-cdev drivers like `myfirst`, but recognising them now means the reader will not be surprised when they show up in Chapter 27 (storage drivers can have many cdevs, one per LUN) or Chapter 28 (network drivers can have many cdevs, one per command channel) 

### Wrapping Up Section 2

Section 2 made the driver's `/dev` presence first-class. The cdev is now created with the modern `make_dev_s` pattern, the cdevsw is fully populated with `D_TRACKCLOSE` and a debug-friendly callback set, the per-cdev state is wired through `si_drv1`, and the detach path drains and destroys the cdev cleanly. The driver still does the same work as `1.6-debug`, but it now exposes that work through a properly-constructed `/dev` node that an administrator can chmod, chown, monitor through `ls`, and reason about from outside the kernel.

In the next section, we turn to the second integration surface: the user-driven control interface that lets a program tell the driver what to do. The vocabulary is `ioctl(2)`, `_IO`, `_IOR`, `_IOW`, `_IOWR`, and a small public header that user-space programs include.

## Section 3: Implementing `ioctl()` Support

### What `ioctl(2)` Is, and Why Drivers Need It

The `read(2)` and `write(2)` system calls are excellent for moving streams of bytes between a user program and a driver. They are not, however, well suited to control. A `read` cannot ask the driver "what is your current state?" without overloading the meaning of the bytes returned. A `write` cannot ask the driver "please reset your statistics" without inventing a private command vocabulary inside the byte stream. The system call that fills this gap is `ioctl(2)`, the input/output control call.

`ioctl` is a side channel for commands. The signature in user space is straightforward: `int ioctl(int fd, unsigned long request, ...);`. The first argument is a file descriptor (in our case, an open `/dev/myfirst0`). The second is a numeric request code that tells the driver what to do. The third is an optional pointer to a structure that carries the parameters of the request, either inbound, outbound, or both. The kernel routes the call into the cdevsw of the cdev backing that file descriptor, into the function pointed at by `d_ioctl`. The driver looks at the request code, performs the corresponding action, and returns 0 on success or a positive `errno` on failure.

Almost every driver that exposes a control interface to user space uses `ioctl` for it. Disk drivers use `ioctl` to report sector size, partition tables, and dump targets. Tape drivers use `ioctl` for rewind, eject, and tension commands. Network drivers use `ioctl` for media changes (`SIOCSIFFLAGS`), MAC address updates, and bus probe commands. Sound drivers use `ioctl` for sample rate, channel count, and buffer size negotiation. The vocabulary is so universal that learning it once unlocks every category of driver in the tree.

For the `myfirst` driver, `ioctl` lets us add commands that have no clean expression as bytes. We can let the operator query the in-memory message length without having to read it. We can let the operator reset the message and the open counters without having to write a special sentinel. We can expose the driver's version number so user-space tools can detect the API they are talking to. Each of these is a one-line change for the operator and a half-page change for the driver, and each is a textbook fit for `ioctl`.

This section walks through the entire ioctl pipeline: the encoding of request codes, the kernel's automatic copyin and copyout, the design of a public header, the implementation of the dispatcher, the construction of a small user-space companion program, and the most common pitfalls. By the end of the section the driver will be at version `1.7-integration-stage-2` and will support four ioctl commands: `MYFIRSTIOC_GETVER`, `MYFIRSTIOC_GETMSG`, `MYFIRSTIOC_SETMSG`, and `MYFIRSTIOC_RESET`.

### How `ioctl` Numbers Are Encoded

An ioctl request code is not an arbitrary integer. It is a packed 32-bit value that encodes four pieces of information in fixed bit fields, defined in `/usr/src/sys/sys/ioccom.h`. The header begins with a comment showing the layout, which is worth reading before going further.

```c
/*
 * Ioctl's have the command encoded in the lower word, and the size of
 * any in or out parameters in the upper word.  The high 3 bits of the
 * upper word are used to encode the in/out status of the parameter.
 *
 *       31 29 28                     16 15            8 7             0
 *      +---------------------------------------------------------------+
 *      | I/O | Parameter Length        | Command Group | Command       |
 *      +---------------------------------------------------------------+
 */
```

The four fields are:

The **direction bits** (bits 29 to 31) tell the kernel whether the third argument to `ioctl` is purely outbound (`IOC_OUT`, the kernel will copy the result back to user space), purely inbound (`IOC_IN`, the kernel will copy the user data into the kernel before the dispatcher runs), bidirectional (`IOC_INOUT`, both directions), or absent (`IOC_VOID`, the request takes no data argument). The kernel uses these bits to decide what `copyin` and `copyout` to perform automatically. The driver itself never has to call `copyin` or `copyout` for a properly encoded ioctl.

The **parameter length** (bits 16 to 28) encodes the size in bytes of the structure passed as the third argument, capped at `IOCPARM_MAX = 8192`. The kernel uses this size to allocate a temporary kernel buffer, perform the appropriate `copyin` or `copyout`, and present the buffer to the dispatcher as the `caddr_t data` argument. A driver that needs to pass more than 8192 bytes through a single ioctl must either embed a pointer in a smaller structure (with the cost of doing its own `copyin`), or use a different mechanism such as `mmap` or `read`.

The **command group** (bits 8 to 15) is a single character that names the family of related ioctls. It is conventionally one of the printable ASCII letters and identifies the subsystem. `'d'` is used by GEOM disk ioctls (`DIOCGMEDIASIZE`, `DIOCGSECTORSIZE`). `'i'` is used by `if_ioctl` (`SIOCSIFFLAGS`). `'t'` is used by terminal ioctls (`TIOCGPTN`). The reader should choose a letter that is not already taken by something the driver might coexist with. For the `myfirst` driver we will use `'M'`.

The **command number** (bits 0 to 7) is a small integer that identifies the specific ioctl within the group. Numbering usually starts at 1 and increases monotonically as commands are added. Reusing a number is a backward-compatibility hazard, so a driver that retires a command should leave the number reserved rather than recycle it.

The macros in `ioccom.h` build these encodings for you. They are the only way to construct ioctl numbers correctly:

```c
#define _IO(g,n)        _IOC(IOC_VOID, (g), (n), 0)
#define _IOR(g,n,t)     _IOC(IOC_OUT,  (g), (n), sizeof(t))
#define _IOW(g,n,t)     _IOC(IOC_IN,   (g), (n), sizeof(t))
#define _IOWR(g,n,t)    _IOC(IOC_INOUT,(g), (n), sizeof(t))
```

`_IO` declares a command that takes no argument. `_IOR` declares a command that returns a `t`-sized result to user space. `_IOW` declares a command that accepts a `t`-sized argument from user space. `_IOWR` declares a command that accepts a `t`-sized argument and writes a `t`-sized result back through the same buffer. The `t` is a type, not a pointer; the macros use `sizeof(t)` to compute the length field.

A few real-world examples make the pattern concrete. From `/usr/src/sys/sys/disk.h`:

```c
#define DIOCGSECTORSIZE _IOR('d', 128, u_int)
#define DIOCGMEDIASIZE  _IOR('d', 129, off_t)
```

These commands are read-only requests for the sector size (returned as `u_int`) and the media size (returned as `off_t`). The group letter `'d'` and the numbers 128 and 129 are reserved for the disk subsystem.

The driver itself never has to decode the bit layout. The command code is opaque to the dispatcher, which compares it to named constants:

```c
switch (cmd) {
case MYFIRSTIOC_GETVER:
        ...
        break;
case MYFIRSTIOC_RESET:
        ...
        break;
default:
        return (ENOIOCTL);
}
```

The kernel uses the bit layout when it sets up the call (to allocate the buffer and perform copyin/copyout), and user space uses it implicitly through the macros. Between those two points, the request code is simply a label.

### Choosing a Group Letter

The choice of a group letter matters because conflicts are silent. Two drivers that pick the same group letter and the same command number will see ioctl requests intended for the other driver if the operator confuses the two. The kernel does not enforce uniqueness across drivers, partly because no central authority assigns letters and partly because most letters are de facto reserved by tradition rather than registration.

A defensive approach is to follow these conventions:

Use **lowercase letters** (`'d'`, `'t'`, `'i'`) only when extending a well-known subsystem whose letter you already know. The lowercase letters are heavily used by base drivers and are easy to collide with.

Use **uppercase letters** (`'M'`, `'X'`, `'Q'`) for fresh drivers that need their own ioctl namespace. There are 26 uppercase letters and far fewer collisions in tree.

Avoid **digits** entirely. They are reserved by historical convention for early subsystems and a new driver that uses one will look out of place to a reviewer.

For the `myfirst` driver we use `'M'`. It is the first letter of the driver name, it is uppercase (so it does not collide with any base subsystem), and it makes the request codes self-documenting in stack traces and in `ktrace` output: a hex dump of an ioctl number with `0x4d` (the ASCII value of `'M'`) in the group field is unambiguously a `myfirst` command.

### The `d_ioctl` Callback Signature

The dispatcher function pointed at by `cdevsw->d_ioctl` has the type `d_ioctl_t`, defined in `/usr/src/sys/sys/conf.h`. The signature is:

```c
typedef int d_ioctl_t(struct cdev *dev, u_long cmd, caddr_t data,
                      int fflag, struct thread *td);
```

The five arguments deserve a slow read.

`dev` is the cdev that backs the file descriptor on which the user called `ioctl`. The driver uses `dev->si_drv1` to recover its softc. This is the same pattern used by every cdevsw callback we have already seen.

`cmd` is the request code, the value the user passed as the second argument to `ioctl`. The driver compares it to the named constants in its public header.

`data` is the kernel's local copy of the third argument. Because the kernel did the copyin and will do the copyout (for `_IOR`, `_IOW`, and `_IOWR` requests), `data` is always a kernel pointer. The driver dereferences it directly without calling `copyin`. For `_IO` requests, `data` is undefined and must not be dereferenced.

`fflag` is the file flags from the open call: `FREAD`, `FWRITE`, or both. The driver can use `fflag` to enforce read-only or write-only access for specific commands. A command that resets state, for example, might require `FWRITE` and return `EBADF` otherwise.

`td` is the calling thread. The driver can use `td` to extract the caller's credentials (`td->td_ucred`), to perform privilege checks (`priv_check_cred(td->td_ucred, PRIV_DRIVER, 0)`), or simply to log the caller's pid. For most commands, `td` is unused.

The return value is 0 on success, or a positive `errno` value on failure. The special value `ENOIOCTL` (defined in `/usr/src/sys/sys/errno.h`) tells the kernel that the driver does not recognise the command, and the kernel will then route the command through the file-system layer's generic ioctl handler. Returning `EINVAL` instead of `ENOIOCTL` for an unknown command is a subtle bug: it tells the kernel "I recognised the command but the arguments are wrong", which suppresses the generic fallback. Always use `ENOIOCTL` for the default case.

### How the Kernel Performs Copyin and Copyout

Before the dispatcher runs, the kernel inspects the direction bits and parameter length encoded in `cmd`. If `IOC_IN` is set, the kernel reads the parameter length, allocates a temporary kernel buffer of that size, copies the user-space argument into it (`copyin`), and passes the kernel buffer as `data`. If `IOC_OUT` is set, the kernel allocates a buffer, calls the dispatcher with the (uninitialised) buffer as `data`, and on a 0 return, copies the buffer back to user space (`copyout`). If both bits are set (`IOC_INOUT`), the kernel does both copyin and copyout around the dispatcher call. If neither is set (`IOC_VOID`), no buffer is allocated and `data` is undefined.

This automation has two consequences worth remembering.

First, the dispatcher writes to and reads from `data` using normal C dereference. The driver never calls `copyin` or `copyout` for a correctly encoded ioctl. This is one of the reasons a properly designed ioctl interface is so much simpler to implement than, say, a write-and-read protocol that fakes a control channel.

Second, the parameter type encoded in `_IOW` or `_IOR` must match what the dispatcher actually reads or writes. If the user-space header declares `_IOR('M', 1, uint32_t)` but the dispatcher writes a `uint64_t` into `*(uint64_t *)data`, the dispatcher will overrun the kernel's 4-byte buffer and corrupt adjacent stack memory, panicking the kernel under a `WITNESS`-enabled build and silently corrupting state under a production build. The header is the contract; the dispatcher must honour it byte for byte.

For ioctls with embedded pointers (a struct that contains a `char *buf` pointing at a separate buffer), the kernel cannot copyin or copyout the buffer because it is not part of the structure. The driver must do its own `copyin` and `copyout` for the buffer contents, while the kernel handles the wrapping struct. This pattern is needed for variable-length data and is covered in the pitfalls subsection below.

### Designing a Public Header

A driver that exposes ioctls must publish a header that declares the request codes and the data structures. User-space programs include this header to construct correct calls. The header lives outside the kernel module: it is part of the driver's contract with user space and must be installable on the system (for example, into `/usr/local/include/myfirst/myfirst_ioctl.h`).

The convention is to place the header in the driver's source directory with a `.h` suffix that matches the public name. For the `myfirst` driver the header is `myfirst_ioctl.h`. Its responsibilities are narrow: declare the ioctl numbers, declare the structures used as ioctl arguments, declare any related constants (such as the maximum length of the message field), and nothing else. It must not include kernel-only headers, must not declare kernel-only types, and must compile cleanly when included by a user-space program.

Here is the full header for the chapter's stage 2 driver:

```c
/*
 * myfirst_ioctl.h - public ioctl interface for the myfirst driver.
 *
 * This header is included by both the kernel module and any user-space
 * program that talks to the driver. Keep it self-contained: no kernel
 * headers, no kernel types, no inline functions that pull kernel state.
 */

#ifndef _MYFIRST_IOCTL_H_
#define _MYFIRST_IOCTL_H_

#include <sys/ioccom.h>
#include <sys/types.h>

/*
 * Maximum length of the in-driver message, including the trailing NUL.
 * The driver enforces this on SETMSG; user-space programs that build
 * larger buffers will see EINVAL.
 */
#define MYFIRST_MSG_MAX 256

/*
 * The interface version. Bumped when this header changes in a way that
 * is not backward-compatible. User-space programs should call
 * MYFIRSTIOC_GETVER first and refuse to operate on an unexpected
 * version.
 */
#define MYFIRST_IOCTL_VERSION 1

/*
 * MYFIRSTIOC_GETVER - return the driver's interface version.
 *
 *   ioctl(fd, MYFIRSTIOC_GETVER, &ver);   // ver = 1, 2, ...
 *
 * No FREAD or FWRITE flag is required.
 */
#define MYFIRSTIOC_GETVER  _IOR('M', 1, uint32_t)

/*
 * MYFIRSTIOC_GETMSG - copy the current in-driver message into the
 * caller's buffer. The buffer must be MYFIRST_MSG_MAX bytes; the
 * message is NUL-terminated.
 */
#define MYFIRSTIOC_GETMSG  _IOR('M', 2, char[MYFIRST_MSG_MAX])

/*
 * MYFIRSTIOC_SETMSG - replace the in-driver message. The buffer must
 * be MYFIRST_MSG_MAX bytes; the kernel takes the prefix up to the
 * first NUL or to MYFIRST_MSG_MAX - 1 bytes.
 *
 * Requires FWRITE on the file descriptor.
 */
#define MYFIRSTIOC_SETMSG  _IOW('M', 3, char[MYFIRST_MSG_MAX])

/*
 * MYFIRSTIOC_RESET - reset all per-instance counters and clear the
 * message. Returns 0 on success.
 *
 * Requires FWRITE on the file descriptor.
 */
#define MYFIRSTIOC_RESET   _IO('M', 4)

#endif /* _MYFIRST_IOCTL_H_ */
```

A few details in this header are worth pausing on.

The use of `uint32_t` and `sys/types.h` (rather than `u_int32_t` and `sys/cdefs.h`) keeps the header portable across the FreeBSD base and any program that follows POSIX. The kernel and user-space agree on the size of `uint32_t`, so the encoded length in the request code matches the dispatcher's view of the data.

The maximum message length, `MYFIRST_MSG_MAX = 256`, is well under `IOCPARM_MAX = 8192`, so the kernel will copyin and copyout the message without complaint. A driver that needed to move larger messages would either raise the limit (up to 8192) or switch to the embedded-pointer pattern.

The `MYFIRST_IOCTL_VERSION` constant gives user-space a way to detect API changes. The first ioctl that any program should issue is `MYFIRSTIOC_GETVER`; if the returned version is not what the program was compiled against, the program should refuse to issue further ioctls and print a clear error. This is standard practice for drivers that expect to evolve.

The argument type `char[MYFIRST_MSG_MAX]` is unusual but legal in `_IOR` and `_IOW`. The macro takes `sizeof(t)`, and `sizeof(char[256]) == 256`, so the encoded length is exactly the array size. This is the cleanest way to express a fixed-size buffer in a public ioctl header.

### Implementing the Dispatcher

With the header in hand, the dispatcher is a switch statement that reads the command code, performs the action, and either returns 0 (success) or a positive errno (failure). The dispatcher lives in `myfirst_ioctl.c`, a new source file added to the driver in stage 2.

The complete dispatcher:

```c
/*
 * myfirst_ioctl.c - ioctl dispatcher for the myfirst driver.
 *
 * The d_ioctl callback in myfirst_cdevsw points at myfirst_ioctl.
 * Per-command argument layout is documented in myfirst_ioctl.h, which
 * is shared with user space.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/file.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/malloc.h>
#include <sys/proc.h>

#include "myfirst.h"
#include "myfirst_debug.h"
#include "myfirst_ioctl.h"

SDT_PROBE_DEFINE3(myfirst, , , ioctl,
    "struct myfirst_softc *", "u_long", "int");

int
myfirst_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int fflag,
    struct thread *td)
{
        struct myfirst_softc *sc = dev->si_drv1;
        int error = 0;

        SDT_PROBE3(myfirst, , , ioctl, sc, cmd, fflag);
        DPRINTF(sc, MYF_DBG_IOCTL, "ioctl: cmd=0x%08lx fflag=0x%x\n",
            cmd, fflag);

        mtx_lock(&sc->sc_mtx);

        switch (cmd) {
        case MYFIRSTIOC_GETVER:
                *(uint32_t *)data = MYFIRST_IOCTL_VERSION;
                break;

        case MYFIRSTIOC_GETMSG:
                /*
                 * Copy the current message into the caller's buffer.
                 * The buffer is MYFIRST_MSG_MAX bytes; we always emit
                 * a NUL-terminated string.
                 */
                strlcpy((char *)data, sc->sc_msg, MYFIRST_MSG_MAX);
                break;

        case MYFIRSTIOC_SETMSG:
                if ((fflag & FWRITE) == 0) {
                        error = EBADF;
                        break;
                }
                /*
                 * The kernel has copied MYFIRST_MSG_MAX bytes into
                 * data. Take the prefix up to the first NUL.
                 */
                strlcpy(sc->sc_msg, (const char *)data, MYFIRST_MSG_MAX);
                sc->sc_msglen = strlen(sc->sc_msg);
                DPRINTF(sc, MYF_DBG_IOCTL,
                    "SETMSG: new message is %zu bytes\n", sc->sc_msglen);
                break;

        case MYFIRSTIOC_RESET:
                if ((fflag & FWRITE) == 0) {
                        error = EBADF;
                        break;
                }
                sc->sc_open_count = 0;
                sc->sc_total_reads = 0;
                sc->sc_total_writes = 0;
                bzero(sc->sc_msg, sizeof(sc->sc_msg));
                sc->sc_msglen = 0;
                DPRINTF(sc, MYF_DBG_IOCTL,
                    "RESET: counters and message cleared\n");
                break;

        default:
                error = ENOIOCTL;
                break;
        }

        mtx_unlock(&sc->sc_mtx);
        return (error);
}
```

Several disciplined choices are baked into this dispatcher and they are worth pointing out before the reader writes their own.

The dispatcher takes the softc mutex once at the top and releases it once at the bottom. Every command runs under the mutex. This prevents a `read` from racing a `SETMSG` (the read would otherwise see a half-replaced message buffer) and prevents two simultaneous `RESET` calls from corrupting the counters. The mutex is the same `sc->sc_mtx` introduced earlier in Part IV; we are simply extending its scope to cover ioctl serialisation.

The dispatcher's first action after taking the mutex is a single SDT probe and a single `DPRINTF`. Both report the command and the file flags. The SDT probe lets a DTrace script trace every ioctl in real time; the `DPRINTF` lets an operator turn on `MYF_DBG_IOCTL` and watch the same flow through `dmesg`. Both use the debug infrastructure introduced in Chapter 23 with no new machinery.

The `MYFIRSTIOC_SETMSG` and `MYFIRSTIOC_RESET` paths check `fflag & FWRITE` before they mutate state. Without this check, a program that opened the device read-only could change the driver's state, which is a privilege-escalation pattern in some drivers. The check returns `EBADF` (bad file descriptor for the operation) rather than `EPERM` (no permission), because the failure is about the file's open flags rather than about the user's identity.

The default branch returns `ENOIOCTL`, never `EINVAL`. This is the rule from the previous subsection, repeated here because it is the single most common bug in homemade dispatchers.

The `strlcpy` calls in `GETMSG` and `SETMSG` are the safe string copy primitive in the FreeBSD kernel. They guarantee NUL termination and never overrun the destination. The same calls would be `strncpy` in older code; `strlcpy` is the modern preferred form and is what `style(9)` recommends.

### The Softc Additions

Stage 2 extends the softc with two fields and confirms that the existing fields are still in use:

```c
struct myfirst_softc {
        device_t        sc_dev;
        struct cdev    *sc_cdev;
        struct mtx      sc_mtx;

        /* From earlier chapters. */
        uint32_t        sc_debug;
        u_int           sc_open_count;
        u_int           sc_total_reads;
        u_int           sc_total_writes;

        /* New for stage 2. */
        char            sc_msg[MYFIRST_MSG_MAX];
        size_t          sc_msglen;
};
```

The message buffer is a fixed-size array sized to match the public header. Storing it inline (rather than as a pointer to a separately allocated buffer) keeps the lifetime simple: the buffer lives exactly as long as the softc. There is no `malloc` to track and no `free` to forget.

The initialisation in `myfirst_attach` becomes:

```c
strlcpy(sc->sc_msg, "Hello from myfirst", sizeof(sc->sc_msg));
sc->sc_msglen = strlen(sc->sc_msg);
```

The driver now has a default greeting that survives until the operator changes it through `SETMSG`, and survives `unload`/`load` cycles only by virtue of the new value being re-set on each load. (This is the same lifetime as every other softc field; persistence across reboots would require a sysctl tunable, which is the subject of Section 4.)

### Wiring the Dispatcher Into `cdevsw`

The cdevsw declared in stage 1 already had a `.d_ioctl` slot waiting to be filled. Stage 2 fills it:

```c
static struct cdevsw myfirst_cdevsw = {
        .d_version = D_VERSION,
        .d_flags   = D_TRACKCLOSE,
        .d_name    = "myfirst",
        .d_open    = myfirst_open,
        .d_close   = myfirst_close,
        .d_read    = myfirst_read,
        .d_write   = myfirst_write,
        .d_ioctl   = myfirst_ioctl,    /* new */
};
```

The kernel reads this table once, when the module loads. There is no runtime registration step; the cdevsw is part of the driver's static state.

### Building the Stage 2 Driver

The `Makefile` for stage 2 must include the new source file:

```make
KMOD=   myfirst
SRCS=   myfirst.c myfirst_debug.c myfirst_ioctl.c

CFLAGS+= -I${.CURDIR}

SYSDIR?= /usr/src/sys

.include <bsd.kmod.mk>
```

The build commands are unchanged from stage 1:

```console
$ make
$ sudo kldload ./myfirst.ko
$ ls -l /dev/myfirst0
crw-rw---- 1 root wheel 0x... <date> /dev/myfirst0
```

If the build fails because `myfirst_ioctl.h` is not found, check that the `CFLAGS` line includes `-I${.CURDIR}`. If the load fails because of an unresolved symbol such as `myfirst_ioctl`, check that `myfirst_ioctl.c` is listed in `SRCS` and that the function name matches the cdevsw entry.

### The `myfirstctl` User-Space Companion

A driver with an ioctl interface needs a small companion program that exercises it. Without one, the operator has no way to call the ioctls except through a hand-written test or through `devctl(8)`'s ioctl pass-through, which is awkward for routine use.

The companion is `myfirstctl`, a single-file C program that takes a subcommand on the command line and calls the corresponding ioctl. It is intentionally small (under 200 lines) and depends only on the public header.

```c
/*
 * myfirstctl.c - command-line front end to the myfirst driver's ioctls.
 *
 * Build:  cc -o myfirstctl myfirstctl.c
 * Usage:  myfirstctl get-version
 *         myfirstctl get-message
 *         myfirstctl set-message "<text>"
 *         myfirstctl reset
 */

#include <sys/types.h>
#include <sys/ioctl.h>

#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "myfirst_ioctl.h"

#define DEVPATH "/dev/myfirst0"

static void
usage(void)
{
        fprintf(stderr,
            "usage: myfirstctl get-version\n"
            "       myfirstctl get-message\n"
            "       myfirstctl set-message <text>\n"
            "       myfirstctl reset\n");
        exit(EX_USAGE);
}

int
main(int argc, char **argv)
{
        int fd, flags;
        const char *cmd;

        if (argc < 2)
                usage();
        cmd = argv[1];

        /*
         * SETMSG and RESET need write access; the others only need
         * read. Open the device with the right flags so the dispatcher
         * does not return EBADF.
         */
        if (strcmp(cmd, "set-message") == 0 ||
            strcmp(cmd, "reset") == 0)
                flags = O_RDWR;
        else
                flags = O_RDONLY;

        fd = open(DEVPATH, flags);
        if (fd < 0)
                err(EX_OSERR, "open %s", DEVPATH);

        if (strcmp(cmd, "get-version") == 0) {
                uint32_t ver;
                if (ioctl(fd, MYFIRSTIOC_GETVER, &ver) < 0)
                        err(EX_OSERR, "MYFIRSTIOC_GETVER");
                printf("driver ioctl version: %u\n", ver);
        } else if (strcmp(cmd, "get-message") == 0) {
                char buf[MYFIRST_MSG_MAX];
                if (ioctl(fd, MYFIRSTIOC_GETMSG, buf) < 0)
                        err(EX_OSERR, "MYFIRSTIOC_GETMSG");
                printf("%s\n", buf);
        } else if (strcmp(cmd, "set-message") == 0) {
                char buf[MYFIRST_MSG_MAX];
                if (argc < 3)
                        usage();
                strlcpy(buf, argv[2], sizeof(buf));
                if (ioctl(fd, MYFIRSTIOC_SETMSG, buf) < 0)
                        err(EX_OSERR, "MYFIRSTIOC_SETMSG");
        } else if (strcmp(cmd, "reset") == 0) {
                if (ioctl(fd, MYFIRSTIOC_RESET) < 0)
                        err(EX_OSERR, "MYFIRSTIOC_RESET");
        } else {
                usage();
        }

        close(fd);
        return (0);
}
```

Two details deserve note.

The program opens the device with the minimum flags required for the requested operation. `MYFIRSTIOC_GETVER` and `MYFIRSTIOC_GETMSG` work fine with `O_RDONLY`, but `MYFIRSTIOC_SETMSG` and `MYFIRSTIOC_RESET` require `O_RDWR` because the dispatcher checks `fflag & FWRITE`. A user who runs `myfirstctl set-message foo` without the appropriate group membership will see `Permission denied` from `open`; one who has the membership but the dispatcher still rejects them would see `Bad file descriptor` from `ioctl`. Both errors are intelligible.

The `MYFIRSTIOC_RESET` call passes no third argument because the macro `_IO` (with no `R` or `W`) declares a void-data ioctl. The library's `ioctl(2)` is variadic, so calling it with two arguments is legal, but care is needed because an extra argument would be passed but ignored. The convention in this book is to call `_IO` ioctls with exactly two arguments to make the void-data nature clear in the source.

A typical session looks like this:

```console
$ myfirstctl get-version
driver ioctl version: 1
$ myfirstctl get-message
Hello from myfirst
$ myfirstctl set-message "drivers are fun"
$ myfirstctl get-message
drivers are fun
$ myfirstctl reset
$ myfirstctl get-message

$
```

After the `reset`, the message buffer is empty and `myfirstctl get-message` prints a blank line. The counters are also reset, which the next section's sysctl interface will let us verify directly.

### Common Pitfalls With ioctl

The first pitfall is **type-size mismatch between header and dispatcher**. If the header declares `_IOR('M', 1, uint32_t)` but the dispatcher writes a `uint64_t` into `*(uint64_t *)data`, the kernel allocated a 4-byte buffer and the dispatcher writes 8 bytes into it. The 4 extra bytes corrupt whatever sat next to the buffer (often other ioctl arguments or the dispatcher's local stack frame). Under `WITNESS` and `INVARIANTS`, the kernel may catch the overrun and panic; under a production build, the result is silent corruption. The fix is to keep the header and the dispatcher in lockstep, ideally by including the same header in both places (which the chapter's pattern does).

The second pitfall is **embedded pointers**. A struct that contains `char *buf; size_t len;` cannot be safely transferred through a single `_IOW`. The kernel will copyin the struct (the pointer and the length), but the buffer that the pointer points at is in user space and the dispatcher cannot dereference it directly. The dispatcher must call `copyin(uap->buf, kbuf, uap->len)` to transfer the buffer contents itself. Forgetting this step causes the dispatcher to read user-space memory through a kernel pointer, which the kernel's address-space protection will trap as a fault. The fix is either to inline the buffer in the struct (the pattern this chapter uses for the message field) or to add explicit `copyin`/`copyout` calls inside the dispatcher.

The third pitfall is **forgetting to handle `ENOIOCTL` correctly**. A driver that returns `EINVAL` for an unknown command suppresses the kernel's generic ioctl fallback. The user might see `Invalid argument` from a command that should have been silently passed up to the file system layer (such as `FIONBIO` for non-blocking I/O hint). The fix is to use `ENOIOCTL` as the default return.

The fourth pitfall is **changing the wire format of an existing ioctl**. Once a program is compiled against `MYFIRSTIOC_SETMSG` declared with a 256-byte buffer, recompiling the driver with a 512-byte buffer breaks the program: the encoded length in the request code changes, the kernel detects the mismatch (because the user passed a 256-byte buffer with the new 512-byte command), and the `ioctl` call returns `ENOTTY` ("Inappropriate ioctl for device"). The fix is to leave existing ioctls alone and to define new commands with new numbers when the format must change. The `MYFIRST_IOCTL_VERSION` constant lets user-space programs detect such evolution before they issue the affected calls.

The fifth pitfall is **doing slow work in the dispatcher while holding a mutex**. The dispatcher in this section holds `sc->sc_mtx` for the entire switch statement, which is fine because every command is fast (a memcpy, a counter reset, a strlcpy). A real driver that needs to perform a hardware operation that may take milliseconds must drop the mutex first and re-acquire it after, or use a sleepable lock. Holding a non-sleepable mutex across a `tsleep` or `msleep` would panic the kernel.

### Wrapping Up Section 3

Section 3 completed the second integration surface: a properly designed ioctl interface. The driver now exposes four commands through `MYFIRSTIOC_GETVER`, `MYFIRSTIOC_GETMSG`, `MYFIRSTIOC_SETMSG`, and `MYFIRSTIOC_RESET`. The interface is self-describing (any user-space program can call `MYFIRSTIOC_GETVER` to detect the API version), the encoding is explicit (the `_IOR`/`_IOW`/`_IO` macros from `/usr/src/sys/sys/ioccom.h`), and the kernel handles the copyin and copyout automatically based on the bit layout. The companion `myfirstctl` program demonstrates how a user-space tool exercises the interface without ever touching the bytes of the request code itself.

The driver milestone for stage 2 is the addition of `myfirst_ioctl.c` and `myfirst_ioctl.h`, both of which integrate cleanly with the debug infrastructure from Chapter 23 (the `MYF_DBG_IOCTL` mask bit and the `myfirst:::ioctl` SDT probe). The `Makefile` grew one entry in `SRCS` and the cdevsw grew one filled-in callback. Everything else in the driver is unchanged.

In Section 4 we turn to the third integration surface: read-only and read-write knobs that an administrator can query and tune from the shell using `sysctl(8)`. Where ioctl is the right channel for a program that already has the device open, sysctl is the right channel for a script or an operator who wants to inspect or adjust driver state without opening anything. The two interfaces complement each other; most production drivers offer both.

## Section 4: Exposing Metrics Through `sysctl()`

### What `sysctl` Is, and Why Drivers Use It

`sysctl(8)` is the FreeBSD kernel's hierarchical name service. Every name in the tree maps to a piece of kernel state: a constant, a counter, a tunable variable, or a function pointer that produces a value on demand. The tree is rooted at `kern.`, `vm.`, `hw.`, `net.`, `dev.`, and a handful of other top-level prefixes. Any program with the appropriate privileges can read and (for writable nodes) modify these values through the `sysctl(3)` library, the `sysctl(8)` command-line tool, or the `sysctlbyname(3)` convenience interface.

For drivers, the relevant subtree is `dev.<driver_name>.<unit>.*`. The Newbus subsystem creates this prefix automatically for every attached device. A driver named `myfirst` with unit 0 attached gets the prefix `dev.myfirst.0` for free, with no driver code required. The driver's only job is to populate the prefix with named OIDs (object identifiers) for the values it wants to expose.

Why expose state through sysctl rather than ioctl? The two mechanisms answer different questions. Ioctl is the right channel for a program that has already opened the device and wants to issue a command. Sysctl is the right channel for an operator at a shell prompt who wants to inspect or adjust state without opening anything. Most production drivers offer both: the ioctl interface for programs, and the sysctl interface for humans, scripts, and monitoring tools.

The common pattern is that sysctl exposes:

* **counters** that summarise the driver's activity since attach
* **read-only state** such as version numbers, hardware identifiers, and link state
* **read-write tunables** such as debug masks, queue depths, and timeout values
* **boot-time tunables** that are read from `/boot/loader.conf` before the driver attaches

By the end of this section, the `myfirst` driver will expose all four categories under `dev.myfirst.0` and will read its initial debug mask from `/boot/loader.conf`. The driver milestone for stage 3 is the addition of `myfirst_sysctl.c` and a small tree of OIDs.

### The Sysctl Namespace

A complete sysctl name looks like a dotted path. The default Newbus prefix for our driver is:

```text
dev.myfirst.0
```

Underneath this prefix the driver may add anything it likes. The `myfirst` driver in stage 3 will add:

```text
dev.myfirst.0.%desc            "myfirst pseudo-device, integration version 1.7"
dev.myfirst.0.%driver          "myfirst"
dev.myfirst.0.%location        ""
dev.myfirst.0.%pnpinfo         ""
dev.myfirst.0.%parent          "nexus0"
dev.myfirst.0.version          "1.7-integration"
dev.myfirst.0.open_count       0
dev.myfirst.0.total_reads      0
dev.myfirst.0.total_writes     0
dev.myfirst.0.message          "Hello from myfirst"
dev.myfirst.0.debug.mask       0
dev.myfirst.0.debug.classes    "INIT(0x1) OPEN(0x2) IO(0x4) IOCTL(0x8) ..."
```

The first five names (the ones starting with `%`) are added by Newbus automatically and describe the device-tree relationship. The remaining names are the driver's contribution. Of these, `version`, `open_count`, `total_reads`, `total_writes`, and `debug.classes` are read-only; `message` and `debug.mask` are read-write. The `debug` subtree is itself a node, which means it can hold further OIDs as the driver grows.

The reader can already see the result on a system with `myfirst` loaded:

```console
$ sysctl dev.myfirst.0
dev.myfirst.0.debug.classes: INIT(0x1) OPEN(0x2) IO(0x4) IOCTL(0x8) INTR(0x10) DMA(0x20) PWR(0x40) MEM(0x80)
dev.myfirst.0.debug.mask: 0
dev.myfirst.0.message: Hello from myfirst
dev.myfirst.0.total_writes: 0
dev.myfirst.0.total_reads: 0
dev.myfirst.0.open_count: 0
dev.myfirst.0.version: 1.7-integration
dev.myfirst.0.%parent: nexus0
dev.myfirst.0.%pnpinfo:
dev.myfirst.0.%location:
dev.myfirst.0.%driver: myfirst
dev.myfirst.0.%desc: myfirst pseudo-device, integration version 1.7
```

The order of the lines is the order of OID creation, reversed. (Newbus adds the `%`-prefixed names last, so they print first when sysctl walks the list backward. This is cosmetic and has no semantic meaning.)

### Static OIDs Versus Dynamic OIDs

Sysctl OIDs come in two flavours.

A **static OID** is declared at compile time with one of the `SYSCTL_*` macros (`SYSCTL_INT`, `SYSCTL_STRING`, `SYSCTL_ULONG`, and so on). The macro generates a constant data structure that the linker glues into a special section, and the kernel assembles the section into the global tree at boot. Static OIDs are appropriate for system-wide values that exist for the lifetime of the kernel: timer ticks, scheduler statistics, and similar.

A **dynamic OID** is created at runtime with one of the `SYSCTL_ADD_*` functions (`SYSCTL_ADD_INT`, `SYSCTL_ADD_STRING`, `SYSCTL_ADD_PROC`, and so on). The function takes a context, a parent, a name, and a pointer to the underlying data, and inserts a new node into the tree. Dynamic OIDs are appropriate for per-instance values that come and go with a device: a driver creates them in `attach` and tears them down in `detach`.

Driver code uses dynamic OIDs almost exclusively. A driver does not exist at compile time of the kernel; it appears when the module loads, and any sysctl subtree it owns must be built up at attach time and disposed of at detach. The Newbus framework gives every driver a per-device sysctl context and a parent OID specifically for this purpose:

```c
struct sysctl_ctx_list *ctx;
struct sysctl_oid *tree;
struct sysctl_oid_list *child;

ctx = device_get_sysctl_ctx(dev);
tree = device_get_sysctl_tree(dev);
child = SYSCTL_CHILDREN(tree);
```

`device_get_sysctl_ctx` returns the per-device context. The context tracks every OID the driver creates so the framework can free them all in one call when the driver detaches. The driver does not have to track them itself.

`device_get_sysctl_tree` returns the per-device tree node, which is the OID corresponding to `dev.<driver>.<unit>`. The tree was created by Newbus when the device was added.

`SYSCTL_CHILDREN(tree)` extracts the child list from the tree node. This is what the driver passes as the parent argument to subsequent `SYSCTL_ADD_*` calls.

With these three handles in hand, the driver can add any number of OIDs to its subtree:

```c
SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "open_count",
    CTLFLAG_RD, &sc->sc_open_count, 0,
    "Number of times the device has been opened");
```

The `SYSCTL_ADD_UINT` call adds an unsigned-int OID under the parent named `open_count`, with `CTLFLAG_RD` (read-only), backed by `&sc->sc_open_count`, with no special initial value, and with a description. The description is what `sysctl -d dev.myfirst.0.open_count` will print. Always write a useful description; an empty one is a documentation hole.

The matching call for a read-write integer is identical except for the flag:

```c
SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "debug_mask_simple",
    CTLFLAG_RW, &sc->sc_debug, 0,
    "Simple writable debug mask");
```

The `CTLFLAG_RW` flag tells the kernel to allow writes from privileged users (root, or processes with `PRIV_DRIVER`).

For a string, the macro is `SYSCTL_ADD_STRING`:

```c
SYSCTL_ADD_STRING(ctx, child, OID_AUTO, "version",
    CTLFLAG_RD, sc->sc_version, 0,
    "Driver version string");
```

The third-from-last argument is a pointer to the buffer that holds the string, and the second-from-last is the buffer's size (zero means unbounded for read-only strings).

### Handler-Backed OIDs

Some OIDs need more logic than a plain memory access. Reading the OID may need to compute the value from several softc fields; writing the OID may need to validate the new value and update related state. These OIDs use a handler function and the macro `SYSCTL_ADD_PROC`.

A handler has the signature:

```c
static int handler(SYSCTL_HANDLER_ARGS);
```

`SYSCTL_HANDLER_ARGS` is a macro that expands to:

```c
struct sysctl_oid *oidp, void *arg1, intptr_t arg2,
struct sysctl_req *req
```

`oidp` identifies the OID being accessed. `arg1` and `arg2` are the user-supplied arguments registered when the OID was created (typically `arg1` points at the softc and `arg2` is unused or holds a small constant). `req` carries the read/write context: `req->newptr` is non-NULL for a write (and points at the new value the user is supplying), and the handler must call `SYSCTL_OUT(req, value, sizeof(value))` to return a value on a read.

A typical handler that exposes a computed value:

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

The handler computes the value (here, by copying the message length under the mutex), then defers to `sysctl_handle_int`, which does the bookkeeping for the read or write and (for a write) calls back into the handler with the new value already in `*ptr`. The handler-of-handlers pattern is idiomatic; using it correctly avoids reimplementing copyin and copyout for every typed handler.

The handler is registered with `SYSCTL_ADD_PROC`:

```c
SYSCTL_ADD_PROC(ctx, child, OID_AUTO, "message_len",
    CTLTYPE_UINT | CTLFLAG_RD | CTLFLAG_MPSAFE,
    sc, 0, myfirst_sysctl_message_len, "IU",
    "Current length of the in-driver message");
```

Three arguments deserve attention. `CTLTYPE_UINT | CTLFLAG_RD | CTLFLAG_MPSAFE` is the type-and-flag word. `CTLTYPE_UINT` declares the OID's external type (unsigned int); `CTLFLAG_RD` declares it read-only; `CTLFLAG_MPSAFE` declares that the handler is safe to call without the giant lock. The `CTLFLAG_MPSAFE` flag is mandatory for new code; without it, the kernel still works but takes the giant lock around every read, which serialises the entire system on any sysctl access.

The seventh argument is the format string. `"IU"` declares an unsigned int (`I` for integer, `U` for unsigned). The full set is documented in `/usr/src/sys/sys/sysctl.h`: `"I"` for int, `"IU"` for uint, `"L"` for long, `"LU"` for ulong, `"Q"` for int64, `"QU"` for uint64, `"A"` for string, `"S,structname"` for an opaque struct. The `sysctl(8)` command uses the format string to decide how to print the value when invoked without `-x` (the raw hex flag).

### The `myfirst` Sysctl Tree

The full sysctl tree for stage 3 is built in a single function, `myfirst_sysctl_attach`, called from `myfirst_attach` after the cdev has been created. The function is short enough to read end to end:

```c
/*
 * myfirst_sysctl.c - sysctl tree for the myfirst driver.
 *
 * Builds dev.myfirst.<unit>.* with version, counters, message, and a
 * debug subtree (debug.mask, debug.classes).
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/sysctl.h>

#include "myfirst.h"
#include "myfirst_debug.h"

#define MYFIRST_VERSION "1.7-integration"

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

void
myfirst_sysctl_attach(struct myfirst_softc *sc)
{
        device_t dev = sc->sc_dev;
        struct sysctl_ctx_list *ctx;
        struct sysctl_oid *tree;
        struct sysctl_oid_list *child;
        struct sysctl_oid *debug_node;
        struct sysctl_oid_list *debug_child;

        ctx = device_get_sysctl_ctx(dev);
        tree = device_get_sysctl_tree(dev);
        child = SYSCTL_CHILDREN(tree);

        /* Read-only: driver version. */
        SYSCTL_ADD_STRING(ctx, child, OID_AUTO, "version",
            CTLFLAG_RD, MYFIRST_VERSION, 0,
            "Driver version string");

        /* Read-only: counters. */
        SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "open_count",
            CTLFLAG_RD, &sc->sc_open_count, 0,
            "Number of currently open file descriptors");

        SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "total_reads",
            CTLFLAG_RD, &sc->sc_total_reads, 0,
            "Total read() calls since attach");

        SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "total_writes",
            CTLFLAG_RD, &sc->sc_total_writes, 0,
            "Total write() calls since attach");

        /* Read-only: message buffer (no copy through user) */
        SYSCTL_ADD_STRING(ctx, child, OID_AUTO, "message",
            CTLFLAG_RD, sc->sc_msg, sizeof(sc->sc_msg),
            "Current in-driver message");

        /* Read-only handler: message length, computed. */
        SYSCTL_ADD_PROC(ctx, child, OID_AUTO, "message_len",
            CTLTYPE_UINT | CTLFLAG_RD | CTLFLAG_MPSAFE,
            sc, 0, myfirst_sysctl_message_len, "IU",
            "Current length of the in-driver message in bytes");

        /* Subtree: debug.* */
        debug_node = SYSCTL_ADD_NODE(ctx, child, OID_AUTO, "debug",
            CTLFLAG_RD | CTLFLAG_MPSAFE, NULL,
            "Debug controls and class enumeration");
        debug_child = SYSCTL_CHILDREN(debug_node);

        SYSCTL_ADD_UINT(ctx, debug_child, OID_AUTO, "mask",
            CTLFLAG_RW | CTLFLAG_TUN, &sc->sc_debug, 0,
            "Bitmask of enabled debug classes");

        SYSCTL_ADD_STRING(ctx, debug_child, OID_AUTO, "classes",
            CTLFLAG_RD,
            "INIT(0x1) OPEN(0x2) IO(0x4) IOCTL(0x8) "
            "INTR(0x10) DMA(0x20) PWR(0x40) MEM(0x80)",
            0, "Names and bit values of debug classes");
}
```

Three details are worth a closer look.

The `version` OID is backed by a string constant (`MYFIRST_VERSION`), not a softc field. A read-only string OID can point at any stable buffer; the kernel never writes through the pointer. This is safer and simpler than carrying a per-softc copy of the version, and it lets the version be visible through `sysctl` even if the driver fails attach partway through.

The `message` OID points directly into the softc's `sc_msg` field with `CTLFLAG_RD`. A reader that calls `sysctl dev.myfirst.0.message` will get the current value. Because the OID is read-only, sysctl will not write to the buffer, so we do not need a write handler. (A read-write version of this OID would need a handler to validate the input; the read-write path runs through the ioctl interface in stage 2.)

The `debug.mask` OID has `CTLFLAG_RW | CTLFLAG_TUN`. The `RW` flag allows writes from a privileged user. The `TUN` flag tells the kernel to look for a matching tunable in `/boot/loader.conf` and apply it before the OID becomes accessible. (We will set up the loader.conf hook in the next subsection.)

### Wiring Sysctl Into Attach and Detach

The attach path now calls the sysctl builder after the cdev is created:

```c
static int
myfirst_attach(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);
        struct make_dev_args args;
        int error;

        sc->sc_dev = dev;
        mtx_init(&sc->sc_mtx, "myfirst", NULL, MTX_DEF);
        strlcpy(sc->sc_msg, "Hello from myfirst", sizeof(sc->sc_msg));
        sc->sc_msglen = strlen(sc->sc_msg);

        make_dev_args_init(&args);
        args.mda_devsw = &myfirst_cdevsw;
        args.mda_uid = UID_ROOT;
        args.mda_gid = GID_WHEEL;
        args.mda_mode = 0660;
        args.mda_si_drv1 = sc;
        args.mda_unit = device_get_unit(dev);

        error = make_dev_s(&args, &sc->sc_cdev,
            "myfirst%d", device_get_unit(dev));
        if (error != 0) {
                mtx_destroy(&sc->sc_mtx);
                return (error);
        }

        myfirst_sysctl_attach(sc);

        DPRINTF(sc, MYF_DBG_INIT, "attach: cdev created and sysctl tree built\n");
        return (0);
}
```

The detach path is unchanged: it does not have to call `myfirst_sysctl_detach`. The Newbus framework owns the per-device sysctl context and tears it down automatically when the device detaches. The driver only needs to clean up resources that it allocated outside the framework (the cdev and the mutex). This is one of the small but real reasons to prefer the per-device context over a private context.

### Boot-Time Tunables Through `/boot/loader.conf`

A driver can let the operator configure its initial behaviour at boot time by reading values from the loader environment. The loader (`/boot/loader.efi` or `/boot/loader`) parses `/boot/loader.conf` before the kernel starts and exports the variables into a small environment that the kernel can query.

The simplest way to read a loader variable is `TUNABLE_INT_FETCH`:

```c
TUNABLE_INT_FETCH("hw.myfirst.debug_mask_default", &sc->sc_debug);
```

The first argument is the loader variable name. The second is a pointer to the destination, which is also the default if the variable is absent. The call is silent if the variable is absent and writes the parsed value otherwise.

The call goes in `myfirst_attach` before `myfirst_sysctl_attach`. By the time the sysctl tree is built, `sc->sc_debug` already has the loader-supplied value (or the compile-time default), and the `dev.myfirst.0.debug.mask` OID reflects it.

A representative `/boot/loader.conf` entry for the driver looks like:

```ini
myfirst_load="YES"
hw.myfirst.debug_mask_default="0x06"
```

The first line tells the loader to load `myfirst.ko` automatically. The second sets the default debug mask to `MYF_DBG_OPEN | MYF_DBG_IO`. After boot, `sysctl dev.myfirst.0.debug.mask` reports `6` and the operator can modify it at runtime without rebooting.

The naming convention is loose but follows a few practices. Keep loader variables under `hw.<driver>.<knob>` because the `hw.` namespace is conventionally read-only at runtime and is not subject to surprise renames. Use `default` in the variable name when the value is the initial value of a runtime-modifiable OID, to make the relationship clear. Document every loader variable in the driver's man page or in the chapter's reference card (the chapter has one at the end).

### Combining Sysctl With the Debug Mask

The reader will recall from Chapter 23 that the driver already has a `sc->sc_debug` field and a `DPRINTF` macro that consults it. With stage 3 in place, the operator can now manipulate the mask from the shell:

```console
$ sysctl dev.myfirst.0.debug.mask
dev.myfirst.0.debug.mask: 0
$ sysctl dev.myfirst.0.debug.classes
dev.myfirst.0.debug.classes: INIT(0x1) OPEN(0x2) IO(0x4) IOCTL(0x8) ...
$ sudo sysctl dev.myfirst.0.debug.mask=0xff
dev.myfirst.0.debug.mask: 0 -> 255
$ # now every DPRINTF call inside the driver will print
```

The `classes` OID exists precisely to spare the operator from having to memorise the bit values. `sysctl` prints both names together and the operator can copy a hex value off the screen and paste it into the next command.

The same mechanism extends to any other knob the driver may want to expose. A driver that has a tunable timeout would add:

```c
SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "timeout_ms",
    CTLFLAG_RW | CTLFLAG_TUN, &sc->sc_timeout_ms, 0,
    "Operation timeout in milliseconds");
```

A driver that wants per-instance enable/disable of a feature would add a `bool` (declared with `SYSCTL_ADD_BOOL`, which is the modern preferred type for boolean flags) or an int with two valid values (0 and 1).

### Building the Stage 3 Driver

The `Makefile` for stage 3 lists the new source file:

```make
KMOD=   myfirst
SRCS=   myfirst.c myfirst_debug.c myfirst_ioctl.c myfirst_sysctl.c

CFLAGS+= -I${.CURDIR}

SYSDIR?= /usr/src/sys

.include <bsd.kmod.mk>
```

After `make` and `kldload`, the operator can immediately walk the tree:

```console
$ sudo kldload ./myfirst.ko
$ sysctl -a dev.myfirst.0
dev.myfirst.0.debug.classes: INIT(0x1) OPEN(0x2) IO(0x4) IOCTL(0x8) ...
dev.myfirst.0.debug.mask: 0
dev.myfirst.0.message_len: 18
dev.myfirst.0.message: Hello from myfirst
dev.myfirst.0.total_writes: 0
dev.myfirst.0.total_reads: 0
dev.myfirst.0.open_count: 0
dev.myfirst.0.version: 1.7-integration
dev.myfirst.0.%parent: nexus0
dev.myfirst.0.%pnpinfo:
dev.myfirst.0.%location:
dev.myfirst.0.%driver: myfirst
dev.myfirst.0.%desc: myfirst pseudo-device, integration version 1.7
```

Opening and reading the device should immediately bump the counters:

```console
$ cat /dev/myfirst0
Hello from myfirst
$ sysctl dev.myfirst.0.total_reads
dev.myfirst.0.total_reads: 1
$ sysctl dev.myfirst.0.open_count
dev.myfirst.0.open_count: 0
```

The `open_count` shows zero because `cat` opens the device, reads, and immediately closes; by the time `sysctl` runs, the count has returned to zero. To see a non-zero value, hold the device open in another terminal:

```console
# terminal 1
$ exec 3< /dev/myfirst0

# terminal 2
$ sysctl dev.myfirst.0.open_count
dev.myfirst.0.open_count: 1

# terminal 1
$ exec 3<&-

# terminal 2
$ sysctl dev.myfirst.0.open_count
dev.myfirst.0.open_count: 0
```

The shell's `exec 3< /dev/myfirst0` opens the device on file descriptor 3 and leaves it open until `exec 3<&-` closes it. This is a useful technique for inspecting any driver's open-count metric without writing a program.

### Common Pitfalls With Sysctl

The first pitfall is **forgetting `CTLFLAG_MPSAFE`**. Without the flag, the kernel takes the giant lock around the OID's handler. For a read-only integer this is harmless; for a heavily accessed OID it serialises the entire kernel and is a latency disaster. Modern kernel code uses `CTLFLAG_MPSAFE` everywhere; the absence of the flag is a sign that the code predates the move to fine-grained locking and should be reviewed for correctness anyway.

The second pitfall is **using a static OID in driver code**. The `SYSCTL_INT` and `SYSCTL_STRING` macros (without the `_ADD_` prefix) declare static OIDs and place them in a special linker section that is processed at kernel boot. A loadable module that uses these macros will install the OIDs when the module loads, but the OIDs will reference per-instance fields that did not exist at compile time, leading to crashes the moment the operator reads them. The fix is to use the `SYSCTL_ADD_*` family for all driver OIDs.

The third pitfall is **leaking the per-driver context**. A driver that uses its own `sysctl_ctx_init` and `sysctl_ctx_free` (rather than the per-device context returned by `device_get_sysctl_ctx`) must remember to call `sysctl_ctx_free` in detach. Forgetting this leaks every OID the driver created and panics the kernel the next time the operator reads one. The fix is to use the per-device context (which the framework cleans up automatically) wherever possible.

The fourth pitfall is **putting per-instance state in a process-shared OID**. A driver that wants a tunable shared across all its instances might be tempted to put it under `kern.myfirst.foo` or under `dev.myfirst.foo`. The latter looks innocent but breaks: when the second instance attaches, Newbus tries to create `dev.myfirst.0.foo` and `dev.myfirst.1.foo`, and the existing `dev.myfirst.foo` (without the unit) is no longer in scope. The fix is to use either `hw.myfirst.<knob>` for shared tunables or per-instance OIDs for per-instance state, but not both with the same name.

The fifth pitfall is **changing the type of an OID**. An OID declared as `CTLTYPE_UINT` cannot have its type changed without invalidating any user-space program that called `sysctlbyname` against it. The kernel returns `EINVAL` if the user passes a buffer of the wrong size. The fix is to keep the type stable across releases; if a different type is needed, define a new OID name and deprecate the old one.

### Wrapping Up Section 4

Section 4 added the third integration surface: the sysctl tree under `dev.myfirst.0`. The driver now exposes its version, counters, current message, debug mask, and class enumeration, all with descriptive help text and all built with the per-device sysctl context provided by Newbus. The debug mask can be set at boot time through `/boot/loader.conf` and tuned at runtime through `sysctl(8)`. A short stanza in attach builds the entire tree; detach does nothing because the framework cleans up automatically.

The driver milestone for stage 3 is the addition of `myfirst_sysctl.c` and a small extension to `myfirst_attach`. The cdevsw, the ioctl dispatcher, the debug infrastructure, and the rest of the driver are unchanged. The sysctl tree is purely additive.

In Section 5 we look at an optional but illustrative integration target: the network stack. Most drivers will never become network drivers, but understanding how a driver registers an `ifnet` and participates in the `if_*` API gives the reader an example of the pattern the kernel uses for every "subsystem with a registration interface." If the driver is not a network driver, the reader can read Section 5 for context and skip directly to Section 7.

## Section 5: Networking Integration (Optional)

### Why This Section Is Optional

The `myfirst` driver is not a network driver and will not become one in this chapter. The cdevsw, ioctl, and sysctl interfaces we have built are sufficient for it. The reader who is following the chapter to integrate a non-network driver may safely skip ahead to Section 7 and lose nothing essential.

However, networking integration is a perfect illustration of a more general principle: many FreeBSD subsystems offer a registration interface that turns a driver into a participant in a larger framework. The pattern is the same whether the framework is networking, storage, USB, or sound: the driver allocates a framework-defined object, fills in callbacks, calls a registration function, and from that moment forward receives callbacks from the framework. Reading this section, even without writing a network driver, builds the intuition for every other framework integration in the book.

The chapter uses the network stack as its example for two reasons. First, it is the most widely understood framework, so the vocabulary (`ifnet`, `if_attach`, `bpf`) connects to user-visible commands such as `ifconfig(8)` and `tcpdump(8)`. Second, the network registration interface is small enough to walk through end to end without losing the reader. Section 6 then shows the same pattern applied to the CAM storage stack.

### What an `ifnet` Is

`ifnet` is the network stack's per-interface object. It is the network counterpart to the `cdev` we worked with in Section 2. Just as a `cdev` represents one device node under `/dev`, an `ifnet` represents one network interface under `ifconfig`. Every line of `ifconfig -a` corresponds to one `ifnet`.

The `ifnet` is opaque from outside the network stack. Drivers see it through the `if_t` typedef and manipulate it through accessor functions (`if_setflags`, `if_getmtu`, `if_settransmitfn`). The opacity is deliberate: it lets the network stack evolve the `ifnet` internals without breaking every driver every release. New drivers should use the `if_t` API exclusively.

The lifecycle of an `ifnet` in a driver is:

1. **allocate** with `if_alloc(IFT_<type>)`
2. **name** with `if_initname(ifp, "myif", unit)`
3. **fill in callbacks** for ioctl, transmit, init, and similar
4. **attach** with `if_attach(ifp)`, which makes the interface visible
5. **attach to BPF** with `bpfattach(ifp, ...)` so `tcpdump` can see traffic
6. ... interface lives, sees traffic, runs ioctls ...
7. **detach from BPF** with `bpfdetach(ifp)`
8. **detach** with `if_detach(ifp)`, which removes it from the visible list
9. **free** with `if_free(ifp)`

The lifecycle mirrors the cdev lifecycle (allocate, name, attach, detach, free) almost exactly, which is not a coincidence; both the network stack and devfs evolved from the same registration-interface pattern.

### A Walkthrough Using `disc(4)`

The simplest in-tree example of an `ifnet` driver is `disc(4)`, the discard interface. `disc(4)` accepts packets and silently drops them; its driver code is therefore mostly the integration scaffolding, with no protocol logic to distract the reader. The full driver lives in `/usr/src/sys/net/if_disc.c`.

The relevant function is `disc_clone_create`, which is called whenever the operator runs `ifconfig disc create`:

```c
static int
disc_clone_create(struct if_clone *ifc, int unit, caddr_t params)
{
        struct ifnet     *ifp;
        struct disc_softc *sc;

        sc = malloc(sizeof(struct disc_softc), M_DISC, M_WAITOK | M_ZERO);
        ifp = sc->sc_ifp = if_alloc(IFT_LOOP);
        ifp->if_softc = sc;
        if_initname(ifp, discname, unit);
        ifp->if_mtu = DSMTU;
        ifp->if_flags = IFF_LOOPBACK | IFF_MULTICAST;
        ifp->if_drv_flags = IFF_DRV_RUNNING;
        ifp->if_ioctl = discioctl;
        ifp->if_output = discoutput;
        ifp->if_hdrlen = 0;
        ifp->if_addrlen = 0;
        ifp->if_snd.ifq_maxlen = 20;
        if_attach(ifp);
        bpfattach(ifp, DLT_NULL, sizeof(u_int32_t));

        return (0);
}
```

Step by step:

`malloc` allocates the driver's softc with `M_WAITOK | M_ZERO`. The waitok flag is allowed because clone-create runs in a sleepable context. The zero flag initialises the structure to zero, which lets the driver assume any field it does not explicitly set is zero or NULL.

`if_alloc(IFT_LOOP)` allocates the `ifnet` from the network stack's pool. The `IFT_LOOP` argument identifies the interface type, which the stack uses for SNMP-style reporting and for some default behaviours. Other common types are `IFT_ETHER` (for Ethernet drivers) and `IFT_TUNNEL` (for tunnel pseudo-devices).

`if_initname` sets the user-visible name. `discname` is the string `"disc"`, and `unit` is the unit number passed in by the cloning framework. Together they form `disc0`, `disc1`, and so on.

The next several lines fill in callbacks and per-interface data: the MTU, the flags, the ioctl handler (`discioctl`), the output function (`discoutput`), the maximum send queue length, and so on. This is the network equivalent of the `cdevsw` table from Section 2; the difference is that it is filled into a per-interface object rather than into a static table.

`if_attach(ifp)` makes the interface visible to user space. After this call returns, `ifconfig disc0` works, the interface shows up in `netstat -i`, and protocols can bind to it.

`bpfattach(ifp, DLT_NULL, ...)` attaches the interface to the BPF (Berkeley Packet Filter) machinery, which is what `tcpdump` reads. `DLT_NULL` declares the link-layer type as "no link layer", appropriate for a loopback. An Ethernet driver would call `bpfattach(ifp, DLT_EN10MB, ETHER_HDR_LEN)`. Without `bpfattach`, `tcpdump` cannot see the interface's traffic, even though the interface itself works.

The destruction path mirrors the creation path, in reverse:

```c
static void
disc_clone_destroy(struct ifnet *ifp)
{
        struct disc_softc *sc;

        sc = ifp->if_softc;

        bpfdetach(ifp);
        if_detach(ifp);
        if_free(ifp);

        free(sc, M_DISC);
}
```

`bpfdetach` first, because `tcpdump` may have a reference. `if_detach` next, because the network stack may still be queuing traffic to the interface; `if_detach` drains the queue and removes the interface from the visible list. `if_free` last, because the `ifnet` may still be referenced by sockets that the upper layers have not yet finished cleaning up; `if_free` defers the actual free until the last reference goes away.

The `disc(4)` driver is roughly 200 lines. A real Ethernet driver is closer to 5000, but the integration boilerplate (allocate, initname, attach, bpfattach, detach, bpfdetach, free) is identical. The 4800 extra lines are protocol-specific details: descriptor rings, interrupt handlers, MAC address management, multicast filters, statistics, link-state polling, and so on. Each is its own pattern, and Chapter 28 covers them in detail. The integration framing covered here is the foundation under all of them.

### How an Operator Sees the Result

Once `disc_clone_create` returns successfully, the operator can manipulate the interface from the shell:

```console
$ sudo ifconfig disc create
$ ifconfig disc0
disc0: flags=8049<UP,LOOPBACK,RUNNING,MULTICAST> metric 0 mtu 1500
$ sudo ifconfig disc0 inet 169.254.99.99/32
$ sudo tcpdump -i disc0 &
$ ping -c1 169.254.99.99
... ping output ...
$ sudo ifconfig disc destroy
```

Each of these commands hits a different part of the integration:

* `ifconfig disc create` calls `disc_clone_create`, which builds the `ifnet` and attaches it.
* `ifconfig disc0` reads the `ifnet`'s flags and MTU through the `if_t` accessors.
* `ifconfig disc0 inet 169.254.99.99/32` calls into `discioctl` with `SIOCAIFADDR`, the ioctl that adds an address.
* `tcpdump -i disc0` opens the BPF tap that `bpfattach` created.
* `ping -c1` sends a packet, which routes through `discoutput`, gets dropped, and never returns.
* `ifconfig disc destroy` calls `disc_clone_destroy`, which detaches and frees.

The whole integration is visible at the user-space level. None of the underlying protocol machinery had to change to accommodate the new driver; the network stack's framework already had a slot for it.

### What This Pattern Generalises To

The same registration pattern applies to many other subsystems:

* The **sound stack** (`sys/dev/sound`) uses `pcm_register` and `pcm_unregister` to make a sound device visible. The driver fills in callbacks for buffer playback, mixer access, and channel configuration.
* The **USB stack** (`sys/dev/usb`) uses `usb_attach` and `usb_detach` to register USB device drivers. The driver fills in callbacks for transfer setup, control requests, and disconnection.
* The **GEOM I/O framework** (`sys/geom`) uses `g_attach` and `g_detach` to register storage providers and consumers. The driver fills in callbacks for I/O start, completion, and orphaning.
* The **CAM SIM framework** (`sys/cam`) uses `cam_sim_alloc` and `xpt_bus_register` to register storage adapters. Section 6 walks through this in more detail.
* The **kobj method dispatch system** (which we have already seen behind `device_method_t`) is itself a registration framework: the driver declares a methods table and the kobj subsystem dispatches calls through it.

In every case the steps are the same: allocate the framework's object, fill in callbacks, call the registration function, take traffic, and unregister cleanly. The vocabulary changes, but the rhythm does not.

### Wrapping Up Section 5

Section 5 used the network stack to illustrate a registration-style integration. The driver allocates an `ifnet`, names it, fills in callbacks, attaches it to the stack, attaches it to BPF, takes traffic, and tears down in reverse order at destroy time. The pattern is small and well bounded; the protocol machinery sits behind it and is the subject of Chapter 27.

The reader who is not writing a network driver gets a useful payoff from this section even without applying it to `myfirst`: every other registration-style integration in the FreeBSD kernel follows the same shape. Once the rhythm of allocate-name-fill-attach-trafficdetach-free is internalised, the storage stack in Section 6 will look familiar at a glance.

In Section 6 we apply the same lens to the CAM storage stack. The vocabulary changes (`cam_sim`, `xpt_bus_register`, `xpt_action`, CCBs), but the registration shape is the same.

## Section 6: CAM Storage Integration (Optional)

### Why This Section Is Optional

`myfirst` is not a storage adapter and will not become one. The reader integrating a non-storage driver should skim this section for vocabulary, take note of the registration shape that mirrors Section 5, and continue to Section 7.

The reader who is integrating a storage adapter (a SCSI host bus adapter, an NVMe controller, an emulated virtual storage controller) will find here the bare bones of how CAM expects a driver to talk to it. The complete protocol surface is large enough to fill a chapter on its own and is the topic of Chapter 27; what we cover here is just the integration framing, identical in spirit to the `if_alloc` / `if_attach` framing we used for networking.

### What CAM Is

CAM (Common Access Method) is FreeBSD's storage subsystem above the device-driver layer. It owns the queue of pending I/O requests, the abstract notion of a target and a logical unit number (LUN), the path-routing logic that sends a request to the right adapter, and the set of generic peripheral drivers (`da(4)` for disks, `cd(4)` for optical, `sa(4)` for tape) that turn block I/O into protocol-specific commands. The driver sits underneath CAM and is responsible only for the adapter-specific work of sending commands to the hardware and reporting completions.

The vocabulary CAM uses is small but specific:

* A **SIM** (SCSI Interface Module) is the framework's view of a storage adapter. The driver allocates one with `cam_sim_alloc`, fills in a callback (the action function), and registers it with `xpt_bus_register`. The SIM is the analogue of an `ifnet` for the storage stack.
* A **CCB** (CAM Control Block) is a single I/O request. CAM hands a CCB to the driver through the action callback; the driver inspects the CCB's `func_code`, performs the requested action, fills in the result, and returns the CCB to CAM with `xpt_done`. CCBs are the analogue of `mbuf`s for the storage stack, with the difference that a CCB carries both the request and the response.
* A **path** identifies a destination as a `(bus, target, LUN)` triple. The driver calls `xpt_create_path` to build a path it can use for asynchronous events.
* The **XPT** (Transport Layer) is the central CAM dispatch mechanism. The driver calls `xpt_action` to send a CCB to CAM (or to itself, for self-targeted actions); CAM eventually calls back into the driver's action function for I/O CCBs targeted at the driver's bus.

### The Registration Lifecycle

For a single-channel adapter the registration steps are:

1. Allocate a CAM device queue with `cam_simq_alloc(maxq)`.
2. Allocate a SIM with `cam_sim_alloc(action, poll, "name", softc, unit, mtx, max_tagged, max_dev_transactions, devq)`.
3. Lock the driver's mutex.
4. Register the SIM with `xpt_bus_register(sim, dev, 0)`.
5. Create a path that the driver can use for events: `xpt_create_path(&path, NULL, cam_sim_path(sim), CAM_TARGET_WILDCARD, CAM_LUN_WILDCARD)`.
6. Unlock the mutex.

The cleanup runs in reverse order:

1. Lock the driver's mutex.
2. Free the path with `xpt_free_path(path)`.
3. Deregister the SIM with `xpt_bus_deregister(cam_sim_path(sim))`.
4. Free the SIM with `cam_sim_free(sim, TRUE)`. The `TRUE` argument tells CAM to free the underlying devq as well; pass `FALSE` if the driver wants to retain the devq for reuse.
5. Unlock the mutex.

The `ahci(4)` driver in `/usr/src/sys/dev/ahci/ahci.c` is a good real-world example. Its channel attach path includes the canonical sequence:

```c
ch->sim = cam_sim_alloc(ahciaction, ahcipoll, "ahcich", ch,
    device_get_unit(dev), (struct mtx *)&ch->mtx,
    (ch->quirks & AHCI_Q_NOCCS) ? 1 : min(2, ch->numslots),
    (ch->caps & AHCI_CAP_SNCQ) ? ch->numslots : 0,
    devq);
if (ch->sim == NULL) {
        cam_simq_free(devq);
        device_printf(dev, "unable to allocate sim\n");
        error = ENOMEM;
        goto err1;
}
if (xpt_bus_register(ch->sim, dev, 0) != CAM_SUCCESS) {
        device_printf(dev, "unable to register xpt bus\n");
        error = ENXIO;
        goto err2;
}
if (xpt_create_path(&ch->path, NULL, cam_sim_path(ch->sim),
    CAM_TARGET_WILDCARD, CAM_LUN_WILDCARD) != CAM_REQ_CMP) {
        device_printf(dev, "unable to create path\n");
        error = ENXIO;
        goto err3;
}
```

The `goto` labels (`err1`, `err2`, `err3`) feed into a single cleanup section that unwinds whatever has been allocated so far. This is the standard FreeBSD-driver pattern for failure handling and is exactly the discipline Section 7 will codify.

### The Action Callback

The action callback is the heart of a CAM driver. Its signature is `void action(struct cam_sim *sim, union ccb *ccb)`. The driver inspects `ccb->ccb_h.func_code` and dispatches:

```c
static void
mydriver_action(struct cam_sim *sim, union ccb *ccb)
{
        struct mydriver_softc *sc;

        sc = cam_sim_softc(sim);

        switch (ccb->ccb_h.func_code) {
        case XPT_SCSI_IO:
                mydriver_start_io(sc, ccb);
                /* completion is asynchronous; xpt_done called later */
                return;

        case XPT_RESET_BUS:
                mydriver_reset_bus(sc);
                ccb->ccb_h.status = CAM_REQ_CMP;
                break;

        case XPT_PATH_INQ: {
                struct ccb_pathinq *cpi = &ccb->cpi;

                cpi->version_num = 1;
                cpi->hba_inquiry = PI_SDTR_ABLE | PI_TAG_ABLE;
                cpi->target_sprt = 0;
                cpi->hba_misc = PIM_NOBUSRESET | PIM_SEQSCAN;
                cpi->hba_eng_cnt = 0;
                cpi->max_target = 0;
                cpi->max_lun = 7;
                cpi->initiator_id = 7;
                strncpy(cpi->sim_vid, "FreeBSD", SIM_IDLEN);
                strncpy(cpi->hba_vid, "MyDriver", HBA_IDLEN);
                strncpy(cpi->dev_name, cam_sim_name(sim), DEV_IDLEN);
                cpi->unit_number = cam_sim_unit(sim);
                cpi->bus_id = cam_sim_bus(sim);
                cpi->ccb_h.status = CAM_REQ_CMP;
                break;
        }

        default:
                ccb->ccb_h.status = CAM_REQ_INVALID;
                break;
        }

        xpt_done(ccb);
}
```

Three branches illustrate the patterns:

`XPT_SCSI_IO` is the data path. The driver kicks off an asynchronous I/O (writing descriptors to the hardware, programming DMA, and so on) and returns immediately without calling `xpt_done`. The hardware completes the I/O some milliseconds later, raises an interrupt, the interrupt handler computes the result, fills in the CCB's status, and only then calls `xpt_done`. CAM does not require synchronous completion; the driver may take as long as the hardware takes.

`XPT_RESET_BUS` is a synchronous control. The driver performs the reset, sets `CAM_REQ_CMP`, and falls through to `xpt_done`. There is no asynchronous component.

`XPT_PATH_INQ` is the SIM's self-description. The first time CAM probes the SIM it issues `XPT_PATH_INQ` and reads back the bus characteristics: maximum LUN, supported flags, vendor identifiers, and so on. The driver fills in the structure and returns. Without a correct `XPT_PATH_INQ` response, CAM cannot probe targets behind the SIM and the driver appears registered but inert.

The `default` branch returns `CAM_REQ_INVALID` for any function code the driver does not implement. CAM is forgiving about this; it simply treats the request as unsupported and either falls back to a generic implementation or surfaces the error to the peripheral driver.

### How an Operator Sees the Result

Once a CAM-bearing driver has called `xpt_bus_register`, CAM probes the bus and the user-visible result is one or more entries in `camcontrol devlist`:

```console
$ camcontrol devlist
<MyDriver Volume 1.0>             at scbus0 target 0 lun 0 (pass0,da0)
$ ls /dev/da0
/dev/da0
$ diskinfo /dev/da0
/dev/da0   512 ... ...
```

The `da0` device under `/dev` is a CAM peripheral driver (`da(4)`) wrapping the LUN that CAM discovered behind the SIM. The operator never deals with the SIM directly; they see only the standard `/dev/daN` interface that every block device uses. This is what makes CAM such a productive integration target: write a SIM, get full disk-style I/O for free.

### Pattern Recognition

By now the reader should see the same shape we saw in Section 5:

| Step              | Networking          | CAM                     |
|-------------------|---------------------|-------------------------|
| Allocate object   | `if_alloc`          | `cam_sim_alloc`         |
| Name and configure| `if_initname`, set callbacks | implicit in `cam_sim_alloc` arguments |
| Attach to framework| `if_attach`        | `xpt_bus_register`      |
| Make discoverable | `bpfattach`         | `xpt_create_path`       |
| Take traffic      | `if_output` callback| action callback         |
| Complete an op    | (synchronous)       | `xpt_done(ccb)`         |
| Detach            | `bpfdetach`, `if_detach` | `xpt_free_path`, `xpt_bus_deregister` |
| Free              | `if_free`           | `cam_sim_free`          |

Other registration interfaces (`pcm_register` for sound, `usb_attach` for USB, `g_attach` for GEOM) follow the same column structure with their own vocabulary. Once the reader sees this table once, every subsequent integration is a matter of looking up the names.

### Wrapping Up Section 6

Section 6 sketched the registration interface for a CAM SIM. The driver allocates a SIM with `cam_sim_alloc`, registers it with `xpt_bus_register`, creates a path for events, takes I/O through the action callback, completes I/O with `xpt_done`, and unregisters in reverse order at detach. The same registration-style integration pattern that we saw with `ifnet` applies, with the obvious change of vocabulary.

The reader has now seen three integration surfaces (devfs, ioctl, sysctl) that almost every driver needs and two registration-style surfaces (network, CAM) that some drivers need. In Section 7 we step back and codify the lifecycle discipline that holds it all together: the order of registration in attach, the order of teardown in detach, and the small set of patterns that distinguish a driver that loads, runs, and unloads cleanly from one that leaks resources or panics on detach.

## Section 7: Registration, Teardown, and Cleanup Discipline

### The Cardinal Rule

A driver that integrates with the kernel through several frameworks (devfs for `/dev`, sysctl for tunables, `ifnet` for networking, CAM for storage, callouts for timers, taskqueues for deferred work, and so on) accumulates a small zoo of allocated objects and registered callbacks. Every one of these has the same property: it must be released in the reverse order in which it was created. Forgetting this turns clean detach into a kernel panic, leaks resources at module unload, and scatters dangling pointers through subsystems the driver no longer owns.

The cardinal rule of integration is therefore very simple to state, even if applying it cleanly requires care:

> **Every successful registration must be paired with a deregistration. The order of deregistration is the reverse of the order of registration. A failed registration must trigger the deregistration of every preceding successful one before the function returns the failure.**

That single sentence describes the entire lifecycle discipline. The rest of this section is a guided tour of how to apply it.

### Why Reverse Order

The reverse-order rule sounds arbitrary; it is not. Each registration is a promise to the framework that "from now until I call deregister, you may call back into me, depend on my state, or hand me work." A framework that has a callback into the driver and that holds work for it cannot safely be torn down while another framework still has access to the same state.

For example, suppose the driver registers a callout, then a cdev, then a sysctl OID. The cdev's `read` callback may consult a value that the callout updates; the callout, in turn, may read state that the sysctl OID exposes. If detach tears down the callout first, then while the cdev is being torn down, a `read` from user space could try to consult a value that the callout was supposed to keep refreshed; the value is now stale and the read returns nonsense. If detach tears down the cdev first, then there is no way for `read` to come in any more, and the callout can be safely cancelled. The order matters.

The general rule is: tear down whatever can call into you, before tearing down what it depends on.

For most drivers the dependency chain is the same as the creation order:

* The cdev depends on the softc (the cdev's callbacks dereference `si_drv1`).
* The sysctl OIDs depend on the softc (they point at softc fields).
* The callouts and taskqueues depend on the softc (they receive a softc pointer as their argument).
* The interrupt handlers depend on the softc, the locks, and any DMA tags.
* The DMA tags and bus resources depend on the device.

If the driver creates these in this order, it should destroy them in the exact reverse order: interrupts first (they can fire at any time), then callouts and taskqueues (they execute at any time), then cdevs (they take user-space calls), then sysctl OIDs (the framework cleans these up automatically), then DMA, then bus resources, then locks. The softc itself is the last thing to free.

### The `goto err1` Pattern in Attach

The hardest place to apply the rule is in attach, when a partial failure can leave the driver halfway initialised. The canonical FreeBSD pattern is the chain of `goto` labels, each of which represents the cleanup needed up to that point:

```c
static int
myfirst_attach(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);
        struct make_dev_args args;
        int error;

        sc->sc_dev = dev;
        mtx_init(&sc->sc_mtx, "myfirst", NULL, MTX_DEF);
        strlcpy(sc->sc_msg, "Hello from myfirst", sizeof(sc->sc_msg));
        sc->sc_msglen = strlen(sc->sc_msg);

        TUNABLE_INT_FETCH("hw.myfirst.debug_mask_default", &sc->sc_debug);

        make_dev_args_init(&args);
        args.mda_devsw = &myfirst_cdevsw;
        args.mda_uid = UID_ROOT;
        args.mda_gid = GID_WHEEL;
        args.mda_mode = 0660;
        args.mda_si_drv1 = sc;
        args.mda_unit = device_get_unit(dev);

        error = make_dev_s(&args, &sc->sc_cdev,
            "myfirst%d", device_get_unit(dev));
        if (error != 0)
                goto fail_mtx;

        myfirst_sysctl_attach(sc);

        DPRINTF(sc, MYF_DBG_INIT, "attach: stage 3 complete\n");
        return (0);

fail_mtx:
        mtx_destroy(&sc->sc_mtx);
        return (error);
}
```

The single error label here exists because there is only one point at which a real failure can occur (the `make_dev_s` call). A more elaborate driver would have one label per registration step. By convention each label is named for the step that failed (`fail_mtx`, `fail_cdev`, `fail_sysctl`), and each label runs the cleanup for every step **above** it in the function. The label that handles the last possible failure is the longest cleanup; the label that handles the first failure is the shortest.

A four-stage attach for a hypothetical hardware driver would look like:

```c
static int
mydriver_attach(device_t dev)
{
        struct mydriver_softc *sc = device_get_softc(dev);
        int error;

        mtx_init(&sc->sc_mtx, "mydriver", NULL, MTX_DEF);

        error = bus_alloc_resource_any(...);
        if (error != 0)
                goto fail_mtx;

        error = bus_setup_intr(...);
        if (error != 0)
                goto fail_resource;

        error = make_dev_s(...);
        if (error != 0)
                goto fail_intr;

        return (0);

fail_intr:
        bus_teardown_intr(...);
fail_resource:
        bus_release_resource(...);
fail_mtx:
        mtx_destroy(&sc->sc_mtx);
        return (error);
}
```

The labels read top-to-bottom in the same order as the cleanup actions execute. A failure at any step jumps to the matching label and falls through the cleanup labels for every preceding successful step. The pattern is so common that reading driver code without it is jarring; reviewers expect to see it.

### The Detach Mirror

Detach should be the exact mirror of a successful attach. Every registration done in attach must have a matching deregistration in detach, in reverse order:

```c
static int
myfirst_detach(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);

        mtx_lock(&sc->sc_mtx);
        if (sc->sc_open_count > 0) {
                mtx_unlock(&sc->sc_mtx);
                return (EBUSY);
        }
        mtx_unlock(&sc->sc_mtx);

        DPRINTF(sc, MYF_DBG_INIT, "detach: tearing down\n");

        /*
         * destroy_dev drains any in-flight cdevsw callbacks. After
         * this call returns, no new open/close/read/write/ioctl can
         * arrive, and no in-flight callback is still running.
         */
        destroy_dev(sc->sc_cdev);

        /*
         * The per-device sysctl context is torn down automatically by
         * the framework after detach returns successfully. Nothing to
         * do here.
         */

        mtx_destroy(&sc->sc_mtx);
        return (0);
}
```

The detach starts by checking `open_count` under the mutex; if anyone holds the device open, detach refuses (returning `EBUSY`) so the operator gets a clear error rather than a panic. After the check, the function tears down whatever attach allocated, in reverse order: cdev first, then sysctl (automatic), then mutex.

The early `EBUSY` return is the "soft" detach pattern. It puts the responsibility for closing the device on the operator: `kldunload myfirst` will fail until the operator runs `pkill cat` (or whatever else is holding the device open). The alternative is the "hard" pattern of refusing detach only if a critical resource is in use and accepting that ordinary file descriptors are a kernel responsibility to drain. The hard pattern is more involved (it usually requires `dev_ref` and `dev_rel`) and is left as a topic for Chapter 27's CAM-driver section.

### Module Event Handlers

Up to this point we have discussed `attach` and `detach`, the per-device lifecycle hooks Newbus calls when a driver instance is added or removed. There is also a per-module lifecycle, controlled by the function registered through `DRIVER_MODULE` (or `MODULE_VERSION` plus a `DECLARE_MODULE`). The kernel calls this function on `MOD_LOAD`, `MOD_UNLOAD`, and `MOD_SHUTDOWN`.

For most drivers the per-module hook is unused. `DRIVER_MODULE` takes a NULL event handler by default, and the kernel does the right thing: on `MOD_LOAD` it adds the driver to the bus's driver list, and on `MOD_UNLOAD` it walks the bus and detaches every instance. The driver author writes only `attach` and `detach`.

Some drivers do need a module-level hook, however. The classic case is a driver that has to set up a global resource (a global hash table, a global mutex, a global event handler) that is shared across all instances. The hook for that is:

```c
static int
myfirst_modevent(module_t mod, int what, void *arg)
{
        switch (what) {
        case MOD_LOAD:
                /* allocate global state */
                return (0);
        case MOD_UNLOAD:
                /* free global state */
                return (0);
        case MOD_SHUTDOWN:
                /* about to power off; flush anything important */
                return (0);
        default:
                return (EOPNOTSUPP);
        }
}

static moduledata_t myfirst_mod = {
        "myfirst", myfirst_modevent, NULL
};
DECLARE_MODULE(myfirst, myfirst_mod, SI_SUB_DRIVERS, SI_ORDER_ANY);
MODULE_VERSION(myfirst, 1);
```

The `myfirst` driver in this chapter does not have global state and therefore does not need a `modevent`. The default `DRIVER_MODULE` machinery is sufficient. We mention the hook here so the reader can recognise it in larger drivers.

### `EVENTHANDLER` for System Events

Some drivers care about events that happen elsewhere in the kernel: a process is forking, the system is shutting down, the network is changing state, and so on. The `EVENTHANDLER` mechanism lets a driver register a callback for a named event:

```c
static eventhandler_tag myfirst_eh_tag;

static void
myfirst_shutdown_handler(void *arg, int howto)
{
        /* called when the system is shutting down */
}

/* In attach: */
myfirst_eh_tag = EVENTHANDLER_REGISTER(shutdown_pre_sync,
    myfirst_shutdown_handler, sc, EVENTHANDLER_PRI_ANY);

/* In detach: */
EVENTHANDLER_DEREGISTER(shutdown_pre_sync, myfirst_eh_tag);
```

The `shutdown_pre_sync`, `shutdown_post_sync`, `shutdown_final`, and `vm_lowmem` event names are the most commonly used in drivers. Each is a documented hook point and each has its own semantics about what the driver may do (sleep, allocate memory, take locks, talk to hardware) inside the callback.

The cardinal rule applies to event handlers exactly as it does to everything else: every successful `EVENTHANDLER_REGISTER` must be paired with an `EVENTHANDLER_DEREGISTER` in the reverse order. Forgetting the deregister leaves a dangling function pointer in the event-handler table; the next time the event fires after module unload, the kernel will jump into freed memory and panic.

### `SYSINIT` for One-Shot Kernel Init

One last machinery worth knowing about is `SYSINIT(9)`, the kernel's compile-time-registered one-shot init mechanism. A `SYSINIT` declaration in driver code:

```c
static void
myfirst_sysinit(void *arg __unused)
{
        /* runs once, very early at kernel boot */
}
SYSINIT(myfirst_init, SI_SUB_DRIVERS, SI_ORDER_FIRST,
    myfirst_sysinit, NULL);
```

declares a function that runs at a specific point during kernel init, before any user-space process exists. `SYSINIT` is rarely needed in driver code; the function does not get re-run when the module reloads, so it does not give the driver a chance to set up per-load state. Most drivers that think they want `SYSINIT` actually want a `MOD_LOAD` event handler instead.

The matching `SYSUNINIT(9)` declaration:

```c
SYSUNINIT(myfirst_uninit, SI_SUB_DRIVERS, SI_ORDER_FIRST,
    myfirst_sysuninit, NULL);
```

declares a function that runs at the corresponding teardown point. The order of declaration matters: `SI_SUB_DRIVERS` runs after `SI_SUB_VFS` but before `SI_SUB_KICK_SCHEDULER`, so a `SYSINIT` at this level can already use the file system but cannot yet schedule processes.

### `bus_generic_detach` and `device_delete_children`

Drivers that are themselves buses (a PCI-to-PCI bridge driver, a USB hub driver, a bus-style virtual driver) have child devices attached to them. Detaching the parent must first detach all the children, in the right order. The framework provides two helpers:

`bus_generic_detach(dev)` walks the device's children and calls `device_detach` on each one. It returns 0 if every child detached successfully, or the first non-zero return code if any child refused.

`device_delete_children(dev)` calls `bus_generic_detach` and then `device_delete_child` for every child, freeing the child device structures.

A bus-style driver's detach should always begin with one of these two:

```c
static int
mybus_detach(device_t dev)
{
        int error;

        error = bus_generic_detach(dev);
        if (error != 0)
                return (error);

        /* now safe to tear down per-bus state */
        ...
        return (0);
}
```

If the driver tears down its bus state before detaching the children, the children find their parent's resources freed out from under them and crash. The order is therefore: detach children first (bus_generic_detach), then tear down per-bus state.

### Putting It All Together

The lifecycle discipline can be summarised in a small checklist that every driver should pass:

1. **Every allocation has a corresponding free.** Track this with a `goto err` chain in attach and a mirror order in detach.
2. **Every registration has a corresponding deregistration.** This applies equally to cdevs, sysctls, callouts, taskqueues, event handlers, interrupt handlers, DMA tags, and bus resources.
3. **The order of teardown is the reverse of the order of setup.** A driver that violates this will leak, panic, or both.
4. **The detach function refuses the operation if any externally-visible resource is still in use.** `EBUSY` is the right return code.
5. **The detach function never frees the softc; the framework does that automatically after detach returns successfully.**
6. **The cdev is destroyed (not freed) with `destroy_dev`, and `destroy_dev` blocks until in-flight callbacks return.**
7. **The per-device sysctl context is torn down automatically; the driver does not call `sysctl_ctx_free` for it.**
8. **Bus-style drivers detach their children first with `bus_generic_detach` or `device_delete_children`, then tear down per-bus state.**
9. **A failed attach unwinds every preceding step before returning the failure code.**
10. **The kernel never sees a half-attached driver: attach either succeeds completely or fails completely.**

The `myfirst` driver of stage 3 passes every item on this checklist; the lab in Section 9 has the reader inject a deliberate failure to see the unwinding in action.

### Wrapping Up Section 7

Section 7 codified the lifecycle discipline that holds every previous section together. The `goto err` chain in attach and the reverse-order teardown in detach are the two patterns the reader will use in every driver they write from now on. Module-level hooks (`MOD_LOAD`, `MOD_UNLOAD`), event-handler registration (`EVENTHANDLER_REGISTER`), and bus-style detach (`bus_generic_detach`) are the variations that some drivers need; for a single-instance pseudo-driver such as `myfirst`, the basic attach/detach pair plus the `goto err` chain is sufficient.

In Section 8 we step back to the chapter's other meta-topic: how the driver evolved from version `1.0` in Part II through `1.5-channels` in Part III, `1.6-debug` in Chapter 23, and now `1.7-integration` here, and how that evolution should be visible in source comments, in the `MODULE_VERSION` declaration, and in user-visible places such as the sysctl `version` OID. The reader will leave Chapter 24 not just with a fully integrated driver but with a discipline for how a driver's version number tells a reader what to expect.

## Section 8: Refactoring and Versioning

### A Driver Has a History

The `myfirst` driver did not appear fully formed. It started in Part II as a single-file demonstration of how `DRIVER_MODULE` works, grew in Part III to support multiple instances and per-channel state, gained debug and tracing infrastructure in Chapter 23, and acquires its full integration surface in this chapter. Each step left the source larger and more capable.

Drivers in the FreeBSD tree have similarly long histories. `null(4)` dates back to 1982; its `cdevsw` has been refactored at least three times to accommodate kernel evolution, but its user-visible behaviour is unchanged. `if_ethersubr.c` predates IPv6 and its API has grown new functions every release while the legacy ones stayed in place. The art of driver maintenance is partly about knowing how to extend a driver without breaking what came before.

This section is a short pause to talk about three closely related disciplines: how to refactor a driver as it grows, how to express the version it is at, and how to decide what counts as a breaking change. The chapter's working example is the transition from `1.6-debug` (the end of Chapter 23) to `1.7-integration` (the end of this chapter), but the patterns generalise to any driver project.

### From One File to Several

The `myfirst` driver in Chapter 23 was a small but real source tree:

```text
myfirst.c          /* probe, attach, detach, cdevsw, read, write */
myfirst.h          /* softc, function declarations */
myfirst_debug.c    /* SDT provider definition */
myfirst_debug.h    /* DPRINTF, debug class bits */
Makefile
```

Stage 3 of this chapter adds two new source files:

```text
myfirst_ioctl.c    /* ioctl dispatcher */
myfirst_ioctl.h    /* PUBLIC ioctl interface for user space */
myfirst_sysctl.c   /* sysctl OID construction */
```

The decision to split each new concern into its own pair of files is deliberate. A single 2000-line `myfirst.c` would compile and load and work, but it would also be harder to read, harder to test, and harder for a co-maintainer to navigate. Splitting along concern lines (open/close vs ioctl vs sysctl vs debug) lets each file fit on a screen and lets the reader understand one concern at a time.

The pattern is roughly:

* `<driver>.c` holds probe, attach, detach, the cdevsw struct, and the small set of cdevsw callbacks (open, close, read, write).
* `<driver>.h` holds the softc, function declarations shared across files, and any private constants. **Not** included by user space.
* `<driver>_debug.c` and `<driver>_debug.h` hold the SDT provider, the DPRINTF macro, the debug class enumeration. **Not** included by user space.
* `<driver>_ioctl.c` holds the ioctl dispatcher. `<driver>_ioctl.h` is the **public** header, includes only `sys/types.h` and `sys/ioccom.h`, and is safe to include from user-space code.
* `<driver>_sysctl.c` holds the sysctl OID construction. **Not** included by user space.

The split between public and private headers matters for two reasons. First, public headers must compile cleanly without kernel context (`_KERNEL` is not defined when user space includes them); a header that pulls in `sys/lock.h` and `sys/mutex.h` will fail to compile from a user-space build. Second, public headers are part of the driver's contract with user space and must be installable into a system-wide location such as `/usr/local/include/myfirst/myfirst_ioctl.h`. A private header that accidentally becomes public is a maintenance trap: every user-space program that includes it pins the driver's internal layout, and any future refactor breaks them.

The `myfirst_ioctl.h` header in this chapter is the driver's only public header. It is small, self-contained, and uses only stable types.

### Version Strings, Version Numbers, and the API Version

A driver carries three different versions, each meaning something different.

The **release version** is the human-readable string printed in `dmesg`, exposed through `dev.<driver>.0.version`, and used in conversation and documentation. The `myfirst` driver uses dotted strings such as `1.6-debug` and `1.7-integration`. The format is convention; what matters is that the string is short, descriptive, and unique per release.

The **module version** is an integer declared with `MODULE_VERSION(<name>, <integer>)`. It is used by the kernel to enforce dependencies between modules. A module that depends on `myfirst` declares `MODULE_DEPEND(other, myfirst, 1, 1, 1)`, where the three integers are the minimum, preferred, and maximum acceptable versions. Bumping the module version signals "I broke compatibility with previous versions; modules that depend on me must be rebuilt."

The **API version** is the integer exposed through `MYFIRSTIOC_GETVER` and stored in the `MYFIRST_IOCTL_VERSION` constant. It is used by user-space programs to detect API drift before they issue ioctls that might fail. Bumping the API version signals "the user-space-visible interface changed in a way that older programs will not handle."

The three versions are independent. The same release may bump only the API version (because a new ioctl was added) without bumping the module version (because in-kernel dependents are unaffected). Conversely, a refactor that changes the layout of an exported in-kernel data structure may bump the module version without bumping the API version, because user space sees no change.

For `myfirst`, the chapter uses these values:

```c
/* myfirst_sysctl.c */
#define MYFIRST_VERSION "1.7-integration"

/* myfirst.c */
MODULE_VERSION(myfirst, 1);

/* myfirst_ioctl.h */
#define MYFIRST_IOCTL_VERSION 1
```

The release is `1.7-integration` because we just landed the integration work. The module version remains `1` because no in-kernel dependents exist. The API version is `1` because this is the first chapter that exposes ioctls; the chapter's stage 2 introduced the interface, and any future change to the ioctl layout would have to bump it.

### When to Bump Each

The rule for bumping the **release version** is "every time the driver changes in a way the operator might care about." Adding a feature, changing default behaviour, fixing a notable bug all qualify. The release version is for humans; it should change often enough that the field is informative.

The rule for bumping the **module version** is "when in-kernel users of the driver would need a recompile to keep working." Adding a new in-kernel function is not a bump (old dependents still work). Removing a function or changing its signature is a bump. Renaming a struct field that other modules read is a bump. A driver that exports nothing in-kernel may keep its module version at 1 forever.

The rule for bumping the **API version** is "when an existing user-space program would misinterpret the driver's responses or fail in a non-obvious way." Adding a new ioctl is not a bump (old programs do not use it). Changing the layout of an existing ioctl's argument structure is a bump. Renumbering an existing ioctl is a bump. A driver that has not yet released to users may freely change the API version while the interface is still being designed; once the first user has shipped against it, every change is a public event.

### Compatibility Shims

A driver that has shipped widely accumulates compatibility shims. The classic shape is a "version 1" ioctl that the driver supports forever next to a "version 2" ioctl that supersedes it. User-space programs that use the v1 interface keep working, programs that use v2 get the new behaviour, and the driver carries both code paths.

The cost of shims is real. Every shim is code that has to be tested, documented, and maintained. Every shim is also a covered API that constrains future refactors. A driver with five shims is harder to evolve than a driver with one.

The discipline is therefore to design carefully up front so that shims are rare. Three habits help:

* **Use named constants, not literal numbers.** A program that says `MYFIRSTIOC_SETMSG` rather than `0x802004d3` will keep working when the driver renumbers the ioctl, because both header and program rebuild against the new header.
* **Prefer additive change over mutating change.** When the driver needs to expose a new field, add a new ioctl rather than extending an existing structure. The old ioctl keeps its layout; the new one carries the extra information.
* **Version every public structure.** A `struct myfirst_v1_args` paired with `MYFIRSTIOC_SETMSG_V1` is a small annotation now and a large compatibility win later.

`myfirst` in this chapter is so small that it does not yet have any shims. The chapter's sole concession to versioning is the `MYFIRSTIOC_GETVER` ioctl, which gives a future maintainer a clean place to add shim logic when the time comes.

### A Worked Refactor: Splitting `myfirst.c`

The transition from Chapter 23's stage 3 (debug) to this chapter's stage 3 (sysctl) is itself a small refactor. The starting source had a single 1000-line `myfirst.c` and a small `myfirst_debug.c`. The ending source has the same `myfirst.c` shrunk by about 100 lines, plus two new files (`myfirst_ioctl.c` and `myfirst_sysctl.c`) that absorb the new logic.

The refactor steps were:

1. Add the two new files with the new logic.
2. Add the new function declarations to `myfirst.h` so the cdevsw can reference `myfirst_ioctl`.
3. Update `myfirst.c` to call `myfirst_sysctl_attach(sc)` from `attach`.
4. Update the `Makefile` to list the new files in `SRCS`.
5. Build, load, exercise, and verify that the driver still passes every Chapter 23 lab.
6. Bump the release version to `1.7-integration`.
7. Add the `MYFIRSTIOC_GETVER` test to the chapter's verification scripts.

Each step is small enough to review on its own. None of them touches the existing logic, which means the refactor is unlikely to introduce regressions in code that worked before. This is the discipline of additive refactoring: grow the driver outward by adding new files and new declarations, leave the existing code in place, and bump the version when the dust settles.

A more aggressive refactor (renaming a function, rearranging a structure, changing the cdevsw's flag set) would need a different discipline: a single commit per change, regression tests run after each, and a clear note in the version bump about what was rearranged. Drivers that ship widely use this discipline for every release; the in-tree `if_em` driver, for example, has a multi-commit refactor in nearly every minor release of FreeBSD, each commit rolled out independently and tested in isolation.

### Three In-Tree Drivers Compared

Three drivers in the FreeBSD source tree illustrate the source-layout discipline at three points along the complexity spectrum. Reading them as a triplet makes the patterns visible.

`/usr/src/sys/dev/null/null.c` is the smallest. It is a single 200-line source file with one `cdevsw` table, one set of callbacks, no separate header, and no debug or sysctl machinery. The whole driver fits on three printed pages. This is the layout for a driver whose entire job is to be present and to absorb (or generate) bytes; integration is only at the cdev layer.

`/usr/src/sys/net/if_disc.c` is a two-file network driver: `if_disc.c` for the driver code and the implicit `if.h` for the framework. The driver registers with the network stack but otherwise has no sysctl tree, no debug subtree, and no public ioctl header (it uses the standard `if_ioctl` set defined by the framework). This is the layout for a driver that is an instance of a framework rather than its own thing; the framework defines the surface, the driver fills in the slots.

`/usr/src/sys/dev/ahci/ahci.c` is a multi-file driver with separate files for the AHCI core, the PCI attach glue, the device-tree FDT attach glue, the enclosure-management code, and the bus-specific logic. Each file is dedicated to one concern; the central file is over 5000 lines but the per-file size is manageable. This is the layout that scales to a real production driver: split by concern, glue by header, and use the file boundary as the unit of refactoring.

The `myfirst` driver in this chapter sits in the middle. Stage 3 has five source files: `myfirst.c` (open/close/read/write and the cdevsw), `myfirst.h` (softc, declarations), `myfirst_debug.c` and `myfirst_debug.h` (debug and SDT), `myfirst_ioctl.c` and `myfirst_ioctl.h` (ioctl, with the latter being public), and `myfirst_sysctl.c` (sysctl tree). This is enough to demonstrate the split-by-concern pattern without the cognitive overhead of a fifty-file driver. A reader who needs to grow `myfirst` further has a clear template: add a new pair of files for the new concern, add the source file to `SRCS`, add the public header to the install set if user space needs it, and update `MYFIRST_VERSION`.

### Wrapping Up Section 8

Section 8 closed the loop on the chapter's other theme: how a driver's source layout, version numbers, and refactor discipline track its evolution. The driver milestone for this chapter is `1.7-integration`, expressed simultaneously as the release string in `MYFIRST_VERSION`, the module version `1` (unchanged because no in-kernel dependents exist), and the API version `1` (set for the first time because this is the first chapter that exposes a stable ioctl interface). Refactoring was kept additive, so no shim was needed.

The reader has now seen the full integration surface: Sections 2 through 4 covered the three universals (devfs, ioctl, sysctl), Sections 5 and 6 sketched the registration-style integrations (network, CAM), Section 7 codified the lifecycle discipline, and Section 8 framed the whole as an evolution that the driver's version numbers should track. The remaining sections of the chapter give the reader hands-on practice with the same material.

### Putting It All Together: The Final Attach and Detach

Before the chapter moves to the labs, it is worth seeing the chapter's complete attach and detach functions in one place. They tie every previous section together: the cdev construction from Section 2, the ioctl wiring from Section 3, the sysctl tree from Section 4, the lifecycle discipline from Section 7, and the version handling from Section 8.

The complete attach for stage 3:

```c
static int
myfirst_attach(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);
        struct make_dev_args args;
        int error;

        /* 1. Stash the device pointer and initialise the lock. */
        sc->sc_dev = dev;
        mtx_init(&sc->sc_mtx, "myfirst", NULL, MTX_DEF);

        /* 2. Initialise the in-driver state to its defaults. */
        strlcpy(sc->sc_msg, "Hello from myfirst", sizeof(sc->sc_msg));
        sc->sc_msglen = strlen(sc->sc_msg);
        sc->sc_open_count = 0;
        sc->sc_total_reads = 0;
        sc->sc_total_writes = 0;
        sc->sc_debug = 0;

        /* 3. Read the boot-time tunable for the debug mask. If the
         *    operator set hw.myfirst.debug_mask_default in
         *    /boot/loader.conf, sc_debug now holds that value;
         *    otherwise sc_debug remains zero.
         */
        TUNABLE_INT_FETCH("hw.myfirst.debug_mask_default", &sc->sc_debug);

        /* 4. Construct the cdev. The args struct gives us a typed,
         *    versionable interface; mda_si_drv1 wires the per-cdev
         *    pointer to the softc atomically, closing the race window
         *    between creation and assignment.
         */
        make_dev_args_init(&args);
        args.mda_devsw = &myfirst_cdevsw;
        args.mda_uid = UID_ROOT;
        args.mda_gid = GID_WHEEL;
        args.mda_mode = 0660;
        args.mda_si_drv1 = sc;
        args.mda_unit = device_get_unit(dev);

        error = make_dev_s(&args, &sc->sc_cdev,
            "myfirst%d", device_get_unit(dev));
        if (error != 0)
                goto fail_mtx;

        /* 5. Build the sysctl tree. The framework owns the per-device
         *    context, so we do not need to track or destroy it
         *    ourselves; detach below does not call sysctl_ctx_free.
         */
        myfirst_sysctl_attach(sc);

        DPRINTF(sc, MYF_DBG_INIT,
            "attach: stage 3 complete, version " MYFIRST_VERSION "\n");
        return (0);

fail_mtx:
        mtx_destroy(&sc->sc_mtx);
        return (error);
}
```

The complete detach for stage 3:

```c
static int
myfirst_detach(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);

        /* 1. Refuse detach if anyone holds the device open. The
         *    chapter's pattern is the simple soft refusal; Challenge 3
         *    walks through the more elaborate dev_ref/dev_rel pattern
         *    that drains in-flight references rather than refusing.
         */
        mtx_lock(&sc->sc_mtx);
        if (sc->sc_open_count > 0) {
                mtx_unlock(&sc->sc_mtx);
                return (EBUSY);
        }
        mtx_unlock(&sc->sc_mtx);

        DPRINTF(sc, MYF_DBG_INIT, "detach: tearing down\n");

        /* 2. Destroy the cdev. destroy_dev blocks until every
         *    in-flight cdevsw callback returns; after this call,
         *    no new open/close/read/write/ioctl can arrive.
         */
        destroy_dev(sc->sc_cdev);

        /* 3. The per-device sysctl context is torn down automatically
         *    by the framework after detach returns successfully.
         *    Nothing to do here.
         */

        /* 4. Destroy the lock. Safe now because the cdev is gone and
         *    no other code path can take it.
         */
        mtx_destroy(&sc->sc_mtx);

        return (0);
}
```

Two things deserve a final note.

The order of operations is the strict reverse of attach: lock first in attach, lock destroyed last in detach; cdev created near the end of attach, cdev destroyed near the start of detach; sysctl tree created last in attach, sysctl tree torn down first (by the framework, automatically) in detach. This is the cardinal rule of Section 7 in concrete form.

The refusal pattern in detach (the `if (open_count > 0)` check) is the chapter's choice for simplicity. A real driver may need the more elaborate `dev_ref`/`dev_rel` machinery to implement a draining detach; Challenge 3 walks through that variant. For `myfirst`, the simple refusal gives the operator a clear error and is sufficient.

In Section 9 we move from explanation to practice. The labs walk the reader through building stage 1, stage 2, and stage 3 of the integration in turn, with verification commands and expected output for each. After the labs come the challenge exercises (Section 10), the troubleshooting catalogue (Section 11), and the wrap-up and bridge that close the chapter.

## Hands-on Labs

The labs in this section take the reader from a freshly cloned working tree through every integration surface added in the chapter. Each lab is small enough to complete in one sitting and is paired with a verification command that confirms the change. Run the labs in order; later labs build on earlier ones.

The companion files under `examples/part-05/ch24-integration/` contain three staged reference drivers (`stage1-devfs/`, `stage2-ioctl/`, `stage3-sysctl/`) that match the milestones of this chapter. The labs assume the reader either starts from their own end-of-Chapter-23 driver (version `1.6-debug`) or copies the appropriate stage directory into a working location, makes changes there, and consults the matching staged directory if they get stuck.

Each lab uses a real FreeBSD 14.3 system. A virtual machine is fine; do not run these labs on a production host because module load and unload can hang or panic the system if the driver has bugs.

### Lab 1: Build and Load the Stage 1 Driver

**Goal**: bring the driver from the Chapter 23 baseline (`1.6-debug`) up to the stage 1 milestone of this chapter (a properly constructed cdev under `/dev/myfirst0`).

**Setup**:

Start from your own end-of-Chapter-23 working tree (the driver at version `1.6-debug`) or copy the reference tree of Chapter 23's last stage into a lab directory:

```console
$ cp -r ~/myfirst-1.6-debug ~/myfirst-lab1
$ cd ~/myfirst-lab1
$ ls
Makefile  myfirst.c  myfirst.h  myfirst_debug.c  myfirst_debug.h
```

If you want to compare against the already-migrated starting point for the chapter's stage 1 (with `make_dev_s` already applied), consult `examples/part-05/ch24-integration/stage1-devfs/` as a reference solution rather than as a starting directory.

**Step 1**: Open `myfirst.c` and find the existing call to `make_dev`. The Chapter 23 code uses the older single-call form. Replace it with the `make_dev_args` form from Section 2:

```c
struct make_dev_args args;
int error;

make_dev_args_init(&args);
args.mda_devsw = &myfirst_cdevsw;
args.mda_uid = UID_ROOT;
args.mda_gid = GID_WHEEL;
args.mda_mode = 0660;
args.mda_si_drv1 = sc;
args.mda_unit = device_get_unit(dev);

error = make_dev_s(&args, &sc->sc_cdev,
    "myfirst%d", device_get_unit(dev));
if (error != 0) {
        mtx_destroy(&sc->sc_mtx);
        return (error);
}
```

**Step 2**: Add `D_TRACKCLOSE` to the cdevsw flags (it should already have `D_VERSION`):

```c
static struct cdevsw myfirst_cdevsw = {
        .d_version = D_VERSION,
        .d_flags   = D_TRACKCLOSE,
        .d_name    = "myfirst",
        .d_open    = myfirst_open,
        .d_close   = myfirst_close,
        .d_read    = myfirst_read,
        .d_write   = myfirst_write,
};
```

**Step 3**: Confirm `myfirst_open` and `myfirst_close` use `dev->si_drv1` to recover the softc:

```c
static int
myfirst_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
        struct myfirst_softc *sc = dev->si_drv1;
        ...
}
```

**Step 4**: Build and load:

```console
$ make
$ sudo kldload ./myfirst.ko
```

**Verification**:

```console
$ ls -l /dev/myfirst0
crw-rw---- 1 root wheel 0x... <date> /dev/myfirst0
$ sudo cat /dev/myfirst0
Hello from myfirst
$ sudo kldstat | grep myfirst
N    1 0xffff... 1...    myfirst.ko
$ sudo dmesg | tail
... (debug messages from MYF_DBG_INIT)
```

If the `ls` shows the wrong owner, group, or mode, re-check the `mda_uid`, `mda_gid`, and `mda_mode` values. If `cat` returns an empty string, check that `myfirst_read` is filling the user buffer from `sc->sc_msg`. If the load succeeds but the device does not appear, check that the cdevsw is referenced from `make_dev_args`.

**Cleanup**:

```console
$ sudo kldunload myfirst
$ ls -l /dev/myfirst0
ls: /dev/myfirst0: No such file or directory
```

A successful unload removes the device node. If the unload fails with `Device busy`, check that no shell or program has the device open (`fstat | grep myfirst0`).

### Lab 2: Add the ioctl Interface

**Goal**: extend the driver to stage 2 by adding the four ioctl commands from Section 3.

**Setup**:

```console
$ cp -r examples/part-05/ch24-integration/stage1-devfs ~/myfirst-lab2
$ cd ~/myfirst-lab2
```

**Step 1**: Create `myfirst_ioctl.h` from the template in Section 3. Place it in the same directory as the other source files. Include `sys/ioccom.h` and `sys/types.h`. Define `MYFIRST_MSG_MAX = 256` and the four ioctl numbers. Do not include any kernel-only header.

**Step 2**: Create `myfirst_ioctl.c` from the template in Section 3. The dispatcher is a single function `myfirst_ioctl` with the standard `d_ioctl_t` signature.

**Step 3**: Add `myfirst_ioctl.c` to `SRCS` in the `Makefile`:

```make
SRCS=   myfirst.c myfirst_debug.c myfirst_ioctl.c
```

**Step 4**: Update the cdevsw to point `.d_ioctl` at the new dispatcher:

```c
.d_ioctl = myfirst_ioctl,
```

**Step 5**: Add `sc_msg` and `sc_msglen` to the softc and initialise them in attach:

```c
strlcpy(sc->sc_msg, "Hello from myfirst", sizeof(sc->sc_msg));
sc->sc_msglen = strlen(sc->sc_msg);
```

**Step 6**: Build the user-space companion. Place `myfirstctl.c` in the same directory and create a small `Makefile.user`:

```make
CC?= cc
CFLAGS+= -Wall -Werror -I.

myfirstctl: myfirstctl.c myfirst_ioctl.h
        ${CC} ${CFLAGS} -o myfirstctl myfirstctl.c
```

(Note that the indentation must be a tab, not spaces, for `make` to parse the rule.)

Build the kernel module and the companion:

```console
$ make
$ make -f Makefile.user
$ sudo kldload ./myfirst.ko
```

**Verification**:

```console
$ ./myfirstctl get-version
driver ioctl version: 1
$ ./myfirstctl get-message
Hello from myfirst
$ sudo ./myfirstctl set-message "drivers are fun"
$ ./myfirstctl get-message
drivers are fun
$ sudo ./myfirstctl reset
$ ./myfirstctl get-message

$
```

If `set-message` returns `Permission denied`, the issue is that the device is mode `0660` and the user is not in `wheel`. Either run with `sudo` (as the verification commands above do) or change the device group to one the user belongs to with `mda_gid` and reload the module.

If `set-message` returns `Bad file descriptor`, the issue is that `myfirstctl` opened the device read-only. Check that the program selects `O_RDWR` for `set-message` and `reset`.

If any ioctl returns `Inappropriate ioctl for device`, the issue is a mismatch between the encoded length in `myfirst_ioctl.h` and the dispatcher's view of the data. Re-check the `_IOR`/`_IOW` macros and the size of the structures they declare.

**Cleanup**:

```console
$ sudo kldunload myfirst
```

### Lab 3: Add the Sysctl Tree

**Goal**: extend the driver to stage 3 by adding the sysctl OIDs from Section 4 and reading a tunable from `/boot/loader.conf`.

**Setup**:

```console
$ cp -r examples/part-05/ch24-integration/stage2-ioctl ~/myfirst-lab3
$ cd ~/myfirst-lab3
```

**Step 1**: Create `myfirst_sysctl.c` from the template in Section 4. The function `myfirst_sysctl_attach(sc)` builds the entire tree.

**Step 2**: Add `myfirst_sysctl.c` to `SRCS` in the `Makefile`.

**Step 3**: Update `myfirst_attach` to call `TUNABLE_INT_FETCH` and `myfirst_sysctl_attach`:

```c
TUNABLE_INT_FETCH("hw.myfirst.debug_mask_default", &sc->sc_debug);

/* ... after make_dev_s succeeds: */
myfirst_sysctl_attach(sc);
```

**Step 4**: Build and load:

```console
$ make
$ sudo kldload ./myfirst.ko
```

**Verification**:

```console
$ sysctl -a dev.myfirst.0
dev.myfirst.0.debug.classes: INIT(0x1) OPEN(0x2) IO(0x4) IOCTL(0x8) ...
dev.myfirst.0.debug.mask: 0
dev.myfirst.0.message_len: 18
dev.myfirst.0.message: Hello from myfirst
dev.myfirst.0.total_writes: 0
dev.myfirst.0.total_reads: 0
dev.myfirst.0.open_count: 0
dev.myfirst.0.version: 1.7-integration
```

Open the device once and re-check the counters:

```console
$ cat /dev/myfirst0
Hello from myfirst
$ sysctl dev.myfirst.0.total_reads
dev.myfirst.0.total_reads: 1
```

Test the loader-time tunable. Edit `/boot/loader.conf` (back it up first):

```console
$ sudo cp /boot/loader.conf /boot/loader.conf.backup
$ sudo sh -c 'echo hw.myfirst.debug_mask_default=\"0x06\" >> /boot/loader.conf'
```

Note that this only takes effect on the next reboot, and only if the module is loaded by the loader (not by `kldload` after boot). For an interactive test without rebooting, set the value before loading:

```console
$ sudo kenv hw.myfirst.debug_mask_default=0x06
$ sudo kldload ./myfirst.ko
$ sysctl dev.myfirst.0.debug.mask
dev.myfirst.0.debug.mask: 6
```

If the value is 0 instead of 6, check that the `TUNABLE_INT_FETCH` call uses the same string as the `kenv` command. The call must run before `myfirst_sysctl_attach` so the value is in place when the OID is created.

**Cleanup**:

```console
$ sudo kldunload myfirst
$ sudo cp /boot/loader.conf.backup /boot/loader.conf
```

### Lab 4: Walk the Lifecycle by Injecting a Failure

**Goal**: see the `goto err` chain in attach actually unwind by deliberately failing one of the steps.

**Setup**:

```console
$ cp -r examples/part-05/ch24-integration/stage3-sysctl ~/myfirst-lab4
$ cd ~/myfirst-lab4
```

**Step 1**: Open `myfirst.c` and find `myfirst_attach`. Insert a deliberate failure right after `make_dev_s` succeeds:

```c
error = make_dev_s(&args, &sc->sc_cdev,
    "myfirst%d", device_get_unit(dev));
if (error != 0)
        goto fail_mtx;

/* DELIBERATE FAILURE for Lab 4 */
device_printf(dev, "Lab 4: injected failure after make_dev_s\n");
error = ENXIO;
goto fail_cdev;

myfirst_sysctl_attach(sc);
return (0);

fail_cdev:
        destroy_dev(sc->sc_cdev);
fail_mtx:
        mtx_destroy(&sc->sc_mtx);
        return (error);
```

**Step 2**: Build and try to load:

```console
$ make
$ sudo kldload ./myfirst.ko
kldload: an error occurred while loading module myfirst. Please check dmesg(8) for more details.
$ sudo dmesg | tail
myfirst0: Lab 4: injected failure after make_dev_s
```

**Verification**:

```console
$ ls /dev/myfirst0
ls: /dev/myfirst0: No such file or directory
$ kldstat | grep myfirst
$
```

The cdev is gone (the `goto fail_cdev` cleanup destroyed it), the module is not loaded, and no resources leaked. If the cdev remains after the failure, the cleanup is missing a `destroy_dev` call. If the kernel panics on the next module load attempt, the cleanup is freeing or destroying something twice.

**Bonus**: change the failure injection to occur **before** `make_dev_s`. The cleanup chain should now skip the `fail_cdev` label and only run `fail_mtx`. Verify that the cdev was never created and that the mutex is destroyed:

```console
$ sudo kldload ./myfirst.ko
$ sudo dmesg | tail
... no Lab 4 message because it now runs before make_dev_s ...
```

**Cleanup**:

Remove the deliberate failure block before continuing.

### Lab 5: Trace the Integration Surfaces with DTrace

**Goal**: use the SDT probes from Chapter 23 to trace ioctl, open, close, and read traffic in real time.

**Setup**: stage 3 driver loaded as in Lab 3.

**Step 1**: Verify the probes are visible to DTrace:

```console
$ sudo dtrace -l -P myfirst
   ID   PROVIDER      MODULE    FUNCTION   NAME
... id  myfirst       kernel    -          open
... id  myfirst       kernel    -          close
... id  myfirst       kernel    -          io
... id  myfirst       kernel    -          ioctl
```

If the list is empty, the SDT probes are not registered. Check that `myfirst_debug.c` is in `SRCS` and that `SDT_PROBE_DEFINE*` is called from there.

**Step 2**: Open a long-running trace in one terminal:

```console
$ sudo dtrace -n 'myfirst:::ioctl { printf("ioctl cmd=0x%x flags=0x%x", arg1, arg2); }'
dtrace: description 'myfirst:::ioctl' matched 1 probe
```

**Step 3**: Exercise the driver in another terminal:

```console
$ ./myfirstctl get-version
$ ./myfirstctl get-message
$ sudo ./myfirstctl set-message "Lab 5"
$ sudo ./myfirstctl reset
```

The DTrace terminal should show one line per ioctl with the command code and the file flags.

**Step 4**: Combine multiple probes into one script:

```console
$ sudo dtrace -n '
    myfirst:::open  { printf("open  pid=%d", pid); }
    myfirst:::close { printf("close pid=%d", pid); }
    myfirst:::io    { printf("io    pid=%d write=%d resid=%d", pid, arg1, arg2); }
    myfirst:::ioctl { printf("ioctl pid=%d cmd=0x%x", pid, arg1); }
'
```

In another terminal:

```console
$ cat /dev/myfirst0
$ ./myfirstctl get-version
$ echo "hello" | sudo tee /dev/myfirst0
```

The DTrace output now shows the complete traffic pattern, with the open and close around each operation, the read or write inside, and any ioctls. This is the value of having SDT probes integrated with the cdevsw callbacks: every integration surface that the driver exposes is also a probe surface for DTrace.

**Cleanup**:

```console
^C
$ sudo kldunload myfirst
```

### Lab 6: Integration Smoke Test

**Goal**: build a single shell script that exercises every integration surface in one run and produces a green/red summary that the reader can paste into a bug report or a release-readiness checklist.

A smoke test is a small, fast, end-to-end check that the driver is alive and that every surface responds. It does not replace careful unit testing; it gives the reader a five-second confirmation that nothing is obviously broken before they invest more time. Real drivers have smoke tests; the chapter recommends adding one to every new driver from day one.

**Setup**: stage 3 driver loaded.

**Step 1**: Create `smoke.sh` in the working directory:

```sh
#!/bin/sh
# smoke.sh - end-to-end smoke test for the myfirst driver.

set -u
fail=0

check() {
        if eval "$1"; then
                printf "  PASS  %s\n" "$2"
        else
                printf "  FAIL  %s\n" "$2"
                fail=$((fail + 1))
        fi
}

echo "=== myfirst integration smoke test ==="

# 1. Module is loaded.
check "kldstat | grep -q myfirst" "module is loaded"

# 2. /dev node exists with the right mode.
check "test -c /dev/myfirst0" "/dev/myfirst0 exists as a character device"
check "test \"\$(stat -f %Lp /dev/myfirst0)\" = \"660\"" "/dev/myfirst0 is mode 0660"

# 3. Sysctl tree is present.
check "sysctl -N dev.myfirst.0.version >/dev/null 2>&1" "version OID is present"
check "sysctl -N dev.myfirst.0.debug.mask >/dev/null 2>&1" "debug.mask OID is present"
check "sysctl -N dev.myfirst.0.open_count >/dev/null 2>&1" "open_count OID is present"

# 4. Ioctls work (requires myfirstctl built).
check "./myfirstctl get-version >/dev/null" "MYFIRSTIOC_GETVER returns success"
check "./myfirstctl get-message >/dev/null" "MYFIRSTIOC_GETMSG returns success"
check "sudo ./myfirstctl set-message smoke && [ \"\$(./myfirstctl get-message)\" = smoke ]" "MYFIRSTIOC_SETMSG round-trip works"
check "sudo ./myfirstctl reset && [ -z \"\$(./myfirstctl get-message)\" ]" "MYFIRSTIOC_RESET clears state"

# 5. Read/write basic path.
check "echo hello | sudo tee /dev/myfirst0 >/dev/null" "write to /dev/myfirst0 succeeds"
check "[ \"\$(cat /dev/myfirst0)\" = hello ]" "read returns the previously written message"

# 6. Counters update.
sudo ./myfirstctl reset >/dev/null
cat /dev/myfirst0 >/dev/null
check "[ \"\$(sysctl -n dev.myfirst.0.total_reads)\" = 1 ]" "total_reads incremented after one read"

# 7. SDT probes are registered.
check "sudo dtrace -l -P myfirst | grep -q open" "myfirst:::open SDT probe is visible"

echo "=== summary ==="
if [ $fail -eq 0 ]; then
        echo "ALL PASS"
        exit 0
else
        printf "%d FAIL\n" "$fail"
        exit 1
fi
```

**Step 2**: Make it executable and run it:

```console
$ chmod +x smoke.sh
$ ./smoke.sh
=== myfirst integration smoke test ===
  PASS  module is loaded
  PASS  /dev/myfirst0 exists as a character device
  PASS  /dev/myfirst0 is mode 0660
  PASS  version OID is present
  PASS  debug.mask OID is present
  PASS  open_count OID is present
  PASS  MYFIRSTIOC_GETVER returns success
  PASS  MYFIRSTIOC_GETMSG returns success
  PASS  MYFIRSTIOC_SETMSG round-trip works
  PASS  MYFIRSTIOC_RESET clears state
  PASS  write to /dev/myfirst0 succeeds
  PASS  read returns the previously written message
  PASS  total_reads incremented after one read
  PASS  myfirst:::open SDT probe is visible
=== summary ===
ALL PASS
```

If any check fails, the script's output points directly at the broken integration surface. A failed `version OID is present` means the sysctl construction did not run; a failed `MYFIRSTIOC_GETVER` means the ioctl dispatcher is not wired correctly; a failed `total_reads incremented` means the read callback is not bumping the counter under the mutex.

**Verification**: re-run after every change to the driver. A passing smoke test before a commit is the cheapest possible insurance against a regression that breaks the basic flow.

### Lab 7: Reload Without Restarting User-Space Programs

**Goal**: confirm that the driver can be unloaded and reloaded while a user-space program holds an open file descriptor open in another terminal.

This test reveals lifecycle bugs that the chapter's "soft detach" pattern is supposed to prevent. A driver that returns `EBUSY` from detach when a user holds the device open is correctly defending itself; a driver that lets detach succeed and then panics when the user issues an ioctl is broken.

**Setup**: stage 3 driver loaded.

**Step 1** (terminal 1): hold the device open with a long-running command:

```console
$ sleep 3600 < /dev/myfirst0 &
$ jobs
[1]+ Running                 sleep 3600 < /dev/myfirst0 &
```

**Step 2** (terminal 2): try to unload:

```console
$ sudo kldunload myfirst
kldunload: can't unload file: Device busy
```

This is the expected behaviour. The chapter's `myfirst_detach` checks `open_count > 0` and returns `EBUSY` rather than tearing down the cdev under an open file descriptor.

**Step 3** (terminal 2): verify that the device is still functional from a different shell:

```console
$ ./myfirstctl get-version
driver ioctl version: 1
$ sysctl dev.myfirst.0.open_count
dev.myfirst.0.open_count: 1
```

The open count reflects the held file descriptor.

**Step 4** (terminal 1): release the file descriptor:

```console
$ kill %1
$ wait
```

**Step 5** (terminal 2): unload now succeeds:

```console
$ sudo kldunload myfirst
$ sysctl dev.myfirst.0
sysctl: unknown oid 'dev.myfirst.0'
```

The OID is gone because Newbus tore down the per-device sysctl context after detach returned successfully.

**Verification**: the unload should succeed every time without a panic. If the kernel panics during step 5, the cause is almost always that the cdev's callbacks are still in flight when `destroy_dev` returns; check that the cdevsw's `d_close` correctly releases anything it acquired in `d_open`, and check that no callout or taskqueue is still scheduled.

A bonus extension is to write a small program that opens the device, immediately calls `MYFIRSTIOC_RESET`, and then loops on `MYFIRSTIOC_GETVER` for several seconds. While the loop is running, try to unload from another terminal. The unload should still fail with `EBUSY`; the in-flight ioctls should not corrupt anything.

### Wrapping Up the Labs

The seven labs walked the reader through the full integration surface, the lifecycle discipline, the smoke test, and the soft-detach contract. Stage 1 added the cdev; stage 2 added the ioctl interface; stage 3 added the sysctl tree; the lab on lifecycle (Lab 4) confirmed the unwind; the DTrace lab (Lab 5) confirmed integration with Chapter 23's debug infrastructure; the smoke test (Lab 6) gave the reader a reusable verification script; and the reload lab (Lab 7) confirmed the soft-detach contract.

A driver that passes all seven labs is at the chapter's milestone version, `1.7-integration`, and is ready for the next chapter's topic. The challenge exercises in Section 10 give the reader optional follow-on work that extends the driver beyond what the chapter covers.

## Challenge Exercises

The challenges below are optional and are intended for the reader who wants to push the driver beyond the chapter's milestone. Each challenge has a stated goal, a few hints about the approach, and a note about which sections of the chapter contain the relevant material. None of the challenges has a single correct answer; readers are encouraged to compare their solution with a reviewer or with the in-tree drivers cited as references.

### Challenge 1: Add a Variable-Length Ioctl

**Goal**: extend the ioctl interface so that a user-space program can transfer a buffer larger than the fixed 256 bytes used by `MYFIRSTIOC_SETMSG`.

The chapter's pattern is fixed-size: `MYFIRSTIOC_SETMSG` declares `_IOW('M', 3, char[256])` and the kernel handles the entire copyin. For larger buffers (say, up to 1 MB), the embedded-pointer pattern is needed:

```c
struct myfirst_blob {
        size_t  len;
        char   *buf;    /* user-space pointer */
};
#define MYFIRSTIOC_SETBLOB _IOW('M', 5, struct myfirst_blob)
```

The dispatcher must call `copyin` to transfer the bytes the pointer references; the structure itself comes through automatic copyin as before. Hints: enforce a maximum length (1 MB is reasonable). Allocate a temporary kernel buffer with `malloc(M_TEMP, len, M_WAITOK)`; do not allocate it inside the softc mutex. Free it before returning. Reference: Section 3, "Common Pitfalls With ioctl", second pitfall.

A bonus extension is to add `MYFIRSTIOC_GETBLOB` that copies the current message in the same variable-length format; pay attention to the case where the user-supplied buffer is shorter than the message and decide whether to truncate, return `ENOMEM`, or write back the required length. Real drivers (`SIOCGIFCAP`, `KIOCGRPC`) use the latter pattern.

### Challenge 2: Add a Per-Open Counter

**Goal**: maintain a per-file-descriptor counter (one number for each open of `/dev/myfirst0`) instead of just the per-instance counter we have now.

The chapter's `sc_open_count` aggregates across all opens. A per-open counter would let a program know how much it has read from its own descriptor. Hints: use `cdevsw->d_priv` to attach a per-fd structure (a `struct myfirst_fdpriv` containing a counter). Allocate the structure in `myfirst_open` and free it in `myfirst_close`. The framework gives each `cdev_priv` a unique pointer in the file's `f_data` field; the read and write callbacks can then look up the per-fd structure through `devfs_get_cdevpriv()`.

Reference: `/usr/src/sys/kern/kern_conf.c` for `devfs_set_cdevpriv` and `devfs_get_cdevpriv`. The pattern is also used by `/usr/src/sys/dev/random/random_harvestq.c`.

A bonus extension is to add a sysctl OID that reports the sum of per-fd counters and verify that it equals the existing aggregate counter at all times. Discrepancies indicate a missing increment somewhere.

### Challenge 3: Implement Soft Detach With `dev_ref`

**Goal**: replace the chapter's "refuse detach if open" pattern with the cleaner "drain to last close, then detach" pattern.

The chapter's detach returns `EBUSY` if any user holds the device open. A more elegant pattern uses `dev_ref`/`dev_rel` to count outstanding references and waits for the count to reach zero before completing detach. Hints: take a `dev_ref` in `myfirst_open` and release it in `myfirst_close`. In detach, set a "going-away" flag, then call `destroy_dev_drain` (or write a small loop that calls `tsleep` while `dev_refs > 0`) before calling `destroy_dev`. Once the count reaches zero and the cdev is destroyed, complete detach normally.

Reference: `/usr/src/sys/kern/kern_conf.c` for the `dev_ref` machinery; `/usr/src/sys/fs/cuse` is a real driver that uses the drain pattern for sleeping detach.

The bonus extension is to add a sysctl OID that reports the current reference count and verify that it matches the open count.

### Challenge 4: Replace the Static Magic Letter

**Goal**: replace the hard-coded `'M'` magic letter in `myfirst_ioctl.h` with a name that does not collide with anything else in the tree.

The chapter chose `'M'` arbitrarily and warned about the risk of collisions. A more defensive driver uses a longer magic identifier and constructs the ioctl numbers from it. Hints: define `MYFIRST_IOC_GROUP = 0x83` (or any byte not used by another driver). The `_IOC` macro then takes that constant rather than a character literal. Document the choice with a comment in the header explaining how it was chosen.

A bonus is to grep `/usr/src/sys` for `_IO[RW]?\\(.\\?'M'` and produce a list of every existing use of `'M'`. (There are several, including `MIDI` ioctls and others; the survey itself is educational.)

### Challenge 5: Add an `EVENTHANDLER` for Shutdown

**Goal**: make the driver behave gracefully when the system is shutting down.

The chapter's driver has no shutdown handler; if the system shuts down with `myfirst` loaded, the framework eventually calls detach. A more polished driver registers an `EVENTHANDLER` for `shutdown_pre_sync` so it can flush any in-flight state before the file system goes read-only.

Hints: register the handler in attach with `EVENTHANDLER_REGISTER(shutdown_pre_sync, ...)`. The handler is called at the corresponding shutdown stage. Deregister in detach with `EVENTHANDLER_DEREGISTER`. Inside the handler, set the driver to a quiescent state (clear the message, zero the counters); at this point the file system is still writable, so any user feedback through `printf` will land in `/var/log/messages` after the next boot.

Reference: Section 7, "EVENTHANDLER for System Events" and `/usr/src/sys/sys/eventhandler.h` for the full list of named events.

### Challenge 6: A Second Per-Driver Sysctl Subtree

**Goal**: add a second subtree under `dev.myfirst.0` that exposes per-thread statistics.

The chapter's tree has a `debug.` subtree. A complete driver might also have a `stats.` subtree (for read/write statistics broken down by file descriptor) or an `errors.` subtree (for error counters). Hints: use `SYSCTL_ADD_NODE` to create a new node, then `SYSCTL_ADD_*` to populate it under the new node's `SYSCTL_CHILDREN`. The pattern is identical to the `debug.` subtree but rooted under a different name.

Reference: Section 4, "The `myfirst` Sysctl Tree" for the existing `debug.` subtree as a model; `/usr/src/sys/dev/iicbus` for several drivers that use multi-subtree sysctl layouts.

### Challenge 7: Cross-Module Dependency

**Goal**: build a second small module (`myfirst_logger`) that depends on `myfirst` and uses its in-kernel API.

The chapter's `myfirst` driver does not export any symbols for in-kernel users. Adding a second module that calls into `myfirst` exercises the `MODULE_DEPEND` machinery. Hints: declare a symbol-bearing function in `myfirst.h` (perhaps `int myfirst_get_message(int unit, char *buf, size_t len)`) and implement it in `myfirst.c`. Build the second module with `MODULE_DEPEND(myfirst_logger, myfirst, 1, 1, 1)` so the kernel loads `myfirst` automatically when `myfirst_logger` loads.

A bonus is to bump the module version of `myfirst` to 2, change the in-kernel API in a non-backward-compatible way, and observe that the second module fails to load until rebuilt against the new version. Reference: Section 8, "Version Strings, Version Numbers, and the API Version".

### Closing the Challenges

The seven challenges range from short (Challenge 4 is mostly a rename and a comment) to substantial (Challenge 3 requires reading and understanding `dev_ref`). The reader who completes all seven will have hands-on familiarity with every integration corner the chapter only sketches. The reader who completes any one will have a deeper feel for the integration discipline than the chapter alone can provide.

## Troubleshooting

The integration surfaces in this chapter sit at the seam between the kernel and the rest of the system. Problems at the seam often look like driver bugs but are really symptoms of a missing flag, a typo in a header, or a misunderstanding about who owns what. The catalogue below collects the most common symptoms, their likely causes, and the fix for each.

### `/dev/myfirst0` Does Not Appear After `kldload`

The first thing to check is whether the module loaded successfully:

```console
$ kldstat | grep myfirst
```

If the module is not listed, the load failed; consult `dmesg` for a more specific message. The most common reason is an unresolved symbol (often because the new source file is not in `SRCS`).

If the module is listed but the device node is missing, the `make_dev_s` call inside `myfirst_attach` likely failed. Add a `device_printf(dev, "make_dev_s returned %d\n", error)` next to the call and re-try. The most common reason for a non-zero return is that another driver already created `/dev/myfirst0` (the kernel will not silently overwrite an existing node) or that `make_dev_s` was called from an unsleepable context with `MAKEDEV_NOWAIT`.

A subtler reason is that `cdevsw->d_version` does not equal `D_VERSION`. The kernel checks this and refuses to register a cdevsw with a version mismatch. The fix is `static struct cdevsw myfirst_cdevsw = { .d_version = D_VERSION, ... };` exactly.

### `cat /dev/myfirst0` Returns "Permission denied"

The device exists but the user cannot open it. The default mode in this chapter is `0660` and the default group is `wheel`. Either run with `sudo`, change `mda_gid` to the user's group, or change `mda_mode` to `0666` (the latter is fine for a teaching module but a poor choice for a production driver because any local user could open the device).

### `ioctl` Returns "Inappropriate ioctl for device"

The kernel returned `ENOTTY`, which means it could not match the request code to any cdevsw. The two common causes are:

* The driver's dispatcher returned `ENOIOCTL` for the command. The kernel translates `ENOIOCTL` to `ENOTTY` for user space. The fix is to add a case for the command in the dispatcher's switch statement.

* The encoded length in the request code does not match the actual buffer size used by the program. This happens after a header refactor where the `_IOR` line was edited but the user-space program was not recompiled against the new header. The fix is to recompile the program against the current header and rebuild the module against the same source.

### `ioctl` Returns "Bad file descriptor"

The dispatcher returned `EBADF`, which is the chapter's pattern for "the file is not opened with the correct flags for this command". The fix is to open the device with `O_RDWR` rather than `O_RDONLY` for any command that mutates state. The `myfirstctl` companion program already does this; a custom program may not.

### `sysctl dev.myfirst.0` Shows the Tree but Reads Return "operation not supported"

This usually means the sysctl OID was added with a stale or invalid handler pointer. If the read returns immediately with `EOPNOTSUPP` (95), the cause is almost always that the OID was registered with `CTLTYPE_OPAQUE` and a handler that does not call `SYSCTL_OUT`. The fix is to use one of the typed `SYSCTL_ADD_*` helpers (`SYSCTL_ADD_UINT`, `SYSCTL_ADD_STRING`, `SYSCTL_ADD_PROC` with the correct format string) so the framework knows what to do on a read.

### `sysctl -w dev.myfirst.0.foo=value` Fails With "permission denied"

The OID was probably created with `CTLFLAG_RD` (read-only) when the writable variant `CTLFLAG_RW` was intended. Re-check the flag word in the `SYSCTL_ADD_*` call and rebuild.

If the flag is correct and the failure persists, the user may not be running as root. Sysctl writes require the `PRIV_SYSCTL` privilege by default; use `sudo` for the write.

### `sysctl` Hangs or Causes a Deadlock

The OID handler is taking the giant lock (because `CTLFLAG_MPSAFE` is missing) at the same time as another thread is holding the giant lock and calling into the driver. The fix is to add `CTLFLAG_MPSAFE` to every OID's flag word. Modern kernels assume MPSAFE everywhere; the absence of the flag is a code-review issue.

A subtler cause is a handler that takes the softc mutex while another thread is holding the softc mutex and reading from sysctl. Audit the handler: it should compute the value under the mutex but call `sysctl_handle_*` outside the mutex. The chapter's `myfirst_sysctl_message_len` follows this pattern.

### `kldunload myfirst` Fails With "Device busy"

The detach refused because some user holds the device open. Find them with `fstat | grep myfirst0` and either ask them to close it or kill the process. After they release the device, the unload will succeed.

If `fstat` shows nothing and the unload still fails, the cause is most likely a leaked `dev_ref`. Re-check that every code path in the driver that takes a `dev_ref` also calls `dev_rel`; in particular, any error path inside `myfirst_open` must release any reference taken before the failure.

### `kldunload myfirst` Causes a Kernel Panic

The driver's detach is destroying or freeing something that the kernel is still using. The two most common causes are:

* The detach freed the softc before destroying the cdev. The cdev's callbacks may still be in flight; they dereference `si_drv1`, get garbage, and panic. The fix is the strict order: `destroy_dev` (which drains in-flight callbacks) first, then mutex_destroy, then return; the framework frees the softc.

* The detach forgot to deregister an event handler. The next event fires after unload and jumps into freed memory. The fix is to call `EVENTHANDLER_DEREGISTER` for every `EVENTHANDLER_REGISTER` done in attach.

The `Lock order reversal` and `WITNESS` messages in `dmesg` are useful diagnostics for both cases. A panic with `page fault while in kernel mode` and a corrupted `%rip` value is the second pattern; a panic with `lock order reversal` and a stack trace through both subsystems is the first.

### DTrace Probes Are Not Visible

`dtrace -l -P myfirst` returns nothing even though the module is loaded. The cause is almost always that the SDT probes are declared in a header but not defined anywhere. Probes need both `SDT_PROBE_DECLARE` (in the header, where consumers see them) and `SDT_PROBE_DEFINE*` (in exactly one source file, which owns the probe storage). The chapter's pattern places the defines in `myfirst_debug.c`. If that file is not in `SRCS`, the probes will not be defined and DTrace will see nothing.

A subtler cause is the SDT probe was renamed in the header but the matching `SDT_PROBE_DEFINE*` was not updated. The build still succeeds because the two declarations refer to different symbols, but DTrace sees only the defined name. Audit the header and source for the same probe name.

### sysctl Tree Survives Unload But Hangs the Next Sysctl

This happens when the driver uses its own sysctl context (rather than the per-device one) and forgets to call `sysctl_ctx_free` in detach. The OIDs reference fields in the now-freed softc; the next `sysctl` walk dereferences the freed memory and the kernel either panics or returns garbage. The fix is to switch to `device_get_sysctl_ctx`, which the framework cleans up automatically.

### General Diagnostic Checklist

When something goes wrong and the cause is not obvious, walk through this short list before reaching for `kgdb`:

1. `kldstat | grep <driver>`: is the module actually loaded?
2. `dmesg | tail`: are there any kernel messages mentioning the driver?
3. `ls -l /dev/<driver>0`: does the device node exist with the expected mode?
4. `sysctl dev.<driver>.0.%driver`: is Newbus aware of the device?
5. `fstat | grep <driver>0`: is anyone holding the device open?
6. `dtrace -l -P <driver>`: are the SDT probes registered?
7. Re-read the attach function and check that every step has a matching cleanup in detach.

The first six commands take ten seconds and rule out the bulk of the common problems. The seventh is the slow one but is almost always the eventual answer for any bug that the first six did not surface.

### Frequently Asked Questions

The questions below come up often enough during integration work that the chapter ends with a short FAQ. Each answer is intentionally compact; the relevant section of the chapter has the full discussion.

**Q1. Why use both ioctl and sysctl when they seem to overlap?**

They answer different questions. Ioctl is for a program that has already opened the device and wants to issue a command (request a state, push new state, trigger an action). Sysctl is for an operator at a shell prompt or a script that wants to inspect or adjust state without opening anything. The same value can be exposed through both interfaces, and many production drivers do exactly that: a `MYFIRSTIOC_GETMSG` for programs and a `dev.myfirst.0.message` for humans. Each user picks the channel that fits their context.

**Q2. When should I use mmap instead of read/write/ioctl?**

Use `mmap` when the data is large, accessed at random, and naturally lives at a memory address (a frame buffer, a DMA descriptor ring, a memory-mapped register space). Use `read`/`write` when the data is sequential, byte-oriented, and small per call. Use `ioctl` for control commands. The three are not in opposition; many drivers expose all three (as `vt(4)` does for the console).

**Q3. Why does the chapter use `make_dev_s` rather than `make_dev`?**

`make_dev_s` is the modern preferred form. It returns an explicit error rather than panicking on a duplicate name; it accepts an args structure so new options can be added without churn; and it is what most current drivers use. The older `make_dev` still works but is discouraged for new code.

**Q4. Do I need to declare `D_TRACKCLOSE`?**

You need it if your driver's `d_close` should be called only on the last close of a file descriptor (the natural meaning of "close"). Without it, the kernel calls `d_close` for every close of every duplicated descriptor, which surprises most drivers. Set it in any new cdevsw unless you have a specific reason not to.

**Q5. When should I bump `MODULE_VERSION`?**

When something in the driver's in-kernel API changes incompatibly. Adding new exported symbols is fine; renaming or removing them is a bump. Changing the layout of a publicly visible structure is a bump. Bumping the module version forces dependents (`MODULE_DEPEND` consumers) to be rebuilt.

**Q6. When should I bump the API version constant in my public header?**

When something in the user-visible interface changes incompatibly. Adding a new ioctl is fine; changing the layout of an existing ioctl's argument structure is a bump. Renumbering an existing ioctl is a bump. Bumping the API version lets user-space programs detect incompatibility before they issue calls.

**Q7. Should I detach my OIDs in `myfirst_detach`?**

No, not if you used `device_get_sysctl_ctx` (the per-device context). The framework cleans the per-device context up automatically after a successful detach. You only need explicit cleanup if you used `sysctl_ctx_init` to create your own context.

**Q8. Why does my detach panic with "invalid memory access"?**

Almost always because the cdev's callbacks are still in flight when the driver freed something they reference. The fix is to call `destroy_dev(sc->sc_cdev)` first; `destroy_dev` blocks until every in-flight callback returns. After it returns, the cdev is gone and no new callbacks can arrive. Only then is it safe to free the softc, free the locks, and so on. The strict order is non-negotiable.

**Q9. What is the difference between `dev_ref` / `dev_rel` and `D_TRACKCLOSE`?**

`D_TRACKCLOSE` is a cdevsw flag that controls when the kernel calls `d_close`: with it, only on the last close; without it, on every close. `dev_ref`/`dev_rel` is a reference-counting mechanism that lets the driver delay detach until outstanding references are released. They are unrelated and complementary. The chapter uses `D_TRACKCLOSE` in stage 1; Challenge 3 demonstrates `dev_ref`/`dev_rel`.

**Q10. Why does my sysctl write return EPERM even though I am root?**

Three possible causes. (a) The OID was created with `CTLFLAG_RD` only; add `CTLFLAG_RW`. (b) The OID has `CTLFLAG_SECURE` and the system is at `securelevel > 0`; lower the securelevel or remove the flag. (c) The user is not actually root but is in a jail without `allow.sysvipc` or similar; root inside a jail does not have `PRIV_SYSCTL` for arbitrary OIDs.

**Q11. My sysctl handler takes the giant lock when it should not. What did I forget?**

`CTLFLAG_MPSAFE` in the flag word. Without it, the kernel takes the giant lock around every call into the handler. Add it everywhere; modern kernels assume MPSAFE everywhere.

**Q12. Should I name my ioctl group letter uppercase or lowercase?**

Uppercase for new drivers. Lowercase letters are heavily used by base subsystems (`'d'` for disk, `'i'` for `if_ioctl`, `'t'` for terminal) and the chance of collision is real. Uppercase letters are mostly free, and a fresh driver should choose one of them.

**Q13. My ioctl returns `Inappropriate ioctl for device` and I do not understand why.**

The kernel returned `ENOTTY` because either (a) the dispatcher returned `ENOIOCTL` for the command (add a case for it), or (b) the encoded length in the request code does not match the buffer the user passed (recompile both sides against the same header).

**Q14. Should I use `strncpy` or `strlcpy` in the kernel?**

`strlcpy`. It guarantees NUL termination and never overruns the destination. `strncpy` does neither and is a frequent source of subtle bugs. The FreeBSD `style(9)` man page recommends `strlcpy` for all new code.

**Q15. My module loads but `dmesg` shows no messages from my driver. What is wrong?**

The driver's debug mask is zero. The chapter's `DPRINTF` macro prints only when the mask bit is set. Either set the mask before loading (`kenv hw.myfirst.debug_mask_default=0xff`), or set it after loading (`sysctl dev.myfirst.0.debug.mask=0xff`).

**Q16. Why does the chapter mention DTrace so often?**

Because it is the most productive debugging tool in the FreeBSD kernel and because the Chapter 23 debug infrastructure is designed to integrate with it. SDT probes give the operator a runtime tap into every integration surface without rebuilding the driver. A driver that exposes well-named SDT probes is much easier to debug than one that does not.

**Q17. Can I use this driver as a template for a real hardware driver?**

The integration surface (cdev, ioctl, sysctl) translates directly. The hardware-specific parts (resource allocation, interrupt handling, DMA setup) come in Chapters 18 to 22 of Part IV. A real PCI driver typically combines the structural patterns from Part IV with the integration patterns from this chapter to arrive at a shipping driver.

**Q18. How do I grant a non-root user access to `/dev/myfirst0` without rebuilding the driver?**

Use `devfs.rules(5)`. Add a rule file under `/etc/devfs.rules` that matches the device name and sets the owner, group, or mode at runtime. For example, to let group `operator` read and write `/dev/myfirst*`:

```text
[myfirst_rules=10]
add path 'myfirst*' mode 0660 group operator
```

Enable the ruleset with `devfs_system_ruleset="myfirst_rules"` in `/etc/rc.conf` and `service devfs restart`. The driver's `mda_uid`, `mda_gid`, and `mda_mode` still set the defaults at creation time; `devfs.rules` lets the administrator override them without touching the source.

**Q19. My `SRCS` list keeps growing. Is that a problem?**

Not by itself. The `SRCS` line in the kernel-module `Makefile` lists every source file that compiles into the module; growing the list as new responsibilities get their own file is normal and expected. The chapter's Stage 3 driver already has four source files (`myfirst.c`, `myfirst_debug.c`, `myfirst_ioctl.c`, `myfirst_sysctl.c`), and Chapter 25 will add more. The warning sign is not the number of entries but the lack of structure: if `SRCS` contains unrelated files that were merged together without a naming scheme, the driver has outgrown its layout and deserves a small refactor. Chapter 25 treats that refactor as a first-class habit.

**Q20. What should I do next?**

Read Chapter 25 (advanced topics and practical tips) to turn this integrated driver into a *maintainable* one, work through the chapter's challenges if you want hands-on practice, and look at one of the in-tree drivers cited in the reference card for a fully worked example. The `null(4)` driver is the gentlest entry; the `if_em` Ethernet driver is the most complete; the `ahci(4)` storage driver shows the CAM patterns. Pick the one closest to what you want to build and read it end to end.

## Wrapping Up

This chapter brought `myfirst` from a working but isolated module into a fully integrated FreeBSD driver. The arc was deliberate: each section added one concrete integration surface and ended with the driver more useful and more discoverable than it began. The reader who walked the labs in Section 9 now has, on disk, a driver that exposes a properly constructed cdev under `/dev`, four well-designed ioctls under a public header, a self-describing sysctl tree under `dev.myfirst.0`, a boot-time tunable through `/boot/loader.conf`, and a clean lifecycle that survives load/unload cycles without leaking resources.

The technical milestones along the way were:

* Stage 1 (Section 2) replaced the older `make_dev` call with the modern `make_dev_args` form, populated `D_TRACKCLOSE`, wired `si_drv1` for per-cdev state, and walked the cdev lifecycle from creation through drain to destruction. The driver's `/dev` presence became first-class.

* Stage 2 (Section 3) added the `MYFIRSTIOC_GETVER`, `MYFIRSTIOC_GETMSG`, `MYFIRSTIOC_SETMSG`, and `MYFIRSTIOC_RESET` ioctls and the matching `myfirst_ioctl.h` public header. The dispatcher reuses the debug infrastructure from Chapter 23 (`MYF_DBG_IOCTL` and the `myfirst:::ioctl` SDT probe). The companion `myfirstctl` user-space program demonstrated how a small command-line tool exercises every ioctl without ever decoding a request code by hand.

* Stage 3 (Section 4) added the `dev.myfirst.0.*` sysctl tree, including a `debug.` subtree that lets the operator inspect and modify the debug mask at runtime, a `version` OID that reports the integration release, counters for read and write activity, and a string OID for the current message. The boot-time tunable `hw.myfirst.debug_mask_default` lets the operator pre-load the debug mask before attach.

* Sections 5 and 6 sketched the same registration-style integration applied to the network stack (`if_alloc`, `if_attach`, `bpfattach`) and the CAM storage stack (`cam_sim_alloc`, `xpt_bus_register`, `xpt_action`). The reader who is not building a network or storage driver still gained a useful pattern: every framework registration in FreeBSD uses the same allocate-name-fill-attach-traffic-detach-free shape.

* Section 7 codified the lifecycle discipline that holds it all together: every successful registration must be paired with a deregistration in reverse order, and a failed attach must unwind every prior step before returning. The `goto err` chain is the canonical encoding of this rule.

* Section 8 framed the chapter as one step in a longer arc: `myfirst` started as a one-file demonstration, grew into a multi-file driver across Parts II to IV, gained debug and tracing in Chapter 23, and gained the integration surface here. The release version, the module version, and the API version each track a different aspect of that evolution; bumping each at the right time is the version discipline of a long-lived driver.

The chapter's labs (Section 9) walked the reader through every milestone, the challenges (Section 10) gave the motivated reader follow-on work, and the troubleshooting catalogue (Section 11) gathered the most common symptoms and fixes for quick reference.

The result is a driver milestone (`1.7-integration`) that the reader can take into the next chapter without any unfinished integration work waiting to bite them. The patterns from this chapter (cdev construction, ioctl design, sysctl trees, lifecycle discipline) are also the patterns that the rest of Part V and most of Parts VI and VII will assume the reader knows.

## Bridge to Chapter 25

Chapter 25 (Advanced Topics and Practical Tips) closes Part 5 by turning the integrated driver of this chapter into a *maintainable* one. Where Chapter 24 added the interfaces that let the driver speak to the rest of the system, Chapter 25 teaches the engineering habits that keep those interfaces stable and readable as the driver absorbs the next year of bug fixes, portability changes, and feature requests. The driver grows from `1.7-integration` to `1.8-maintenance`; the visible additions are modest but the discipline behind them is what separates a driver that survives one development cycle from one that survives a decade.

The bridge from Chapter 24 to Chapter 25 has four concrete parts.

First, the rate-limited logging that Chapter 25 introduces sits directly on top of the `DPRINTF` macro from Chapter 23 and the integration surfaces this chapter added. A new `DLOG_RL` macro built around `ppsratecheck(9)` lets the driver keep the same debug classes it already uses but without flooding `dmesg` during an event storm. The discipline is small: choose a per-second limit, fold it into the existing debug call sites, and audit the handful of places where an unrestricted `device_printf` could run in a loop.

Second, the ioctl and sysctl paths this chapter built will be audited in Chapter 25 for a consistent errno vocabulary. The chapter distinguishes `EINVAL` from `ENXIO`, `ENOIOCTL` from `ENOTTY`, `EBUSY` from `EAGAIN`, and `EPERM` from `EACCES`, so that every integration surface returns the right code on every failure. The reader walks the dispatcher written in Section 3 and the sysctl handlers written in Section 4, and adjusts them where the wrong error was returned.

Third, the boot-time tunable `hw.myfirst.debug_mask_default` introduced in Section 4 will be generalised in Chapter 25 into a small but disciplined tunable vocabulary through `TUNABLE_INT_FETCH`, `TUNABLE_LONG_FETCH`, `TUNABLE_BOOL_FETCH`, and `TUNABLE_STR_FETCH`, cooperating with writable sysctls under `CTLFLAG_TUN`. The same `MYFIRST_VERSION`, `MODULE_VERSION`, and `MYFIRST_IOCTL_VERSION` triple this chapter settled on will be extended with a `MYFIRSTIOC_GETCAPS` ioctl so user-space tools can detect features at runtime without trial and error.

Fourth, the `goto err` chain introduced in Section 7 will be promoted from a lab exercise into the driver's production cleanup pattern, and the chapter's refactor will move the Newbus attach logic and the cdev callbacks into separate files (`myfirst_bus.c` and `myfirst_cdev.c`) alongside a `myfirst_log.c` for the new logging macros. Chapter 25 also introduces `SYSINIT(9)` and `SYSUNINIT(9)` for driver-wide initialisation and a `shutdown_pre_sync` event handler through `EVENTHANDLER(9)`, adding two more registration-style surfaces to the ones this chapter already taught.

Read on with the confidence that the integration vocabulary is now in place. Chapter 25 takes this driver and makes it ready for the long haul; Part 6 then begins the transport-specific chapters that lean on every habit Part 5 has built.

## Reference Card and Glossary

The remaining pages of the chapter are a compact reference. They are designed to be read straight through the first time and then jumped into when the reader needs to look something up. The order is: a reference card of the important macros, structures, and flags; a glossary of integration vocabulary; and a short directory of the companion files that ship with the chapter.

### Quick Reference: cdev Construction

| Function | When to use |
|----------|-------------|
| `make_dev_args_init(args)` | Always before `make_dev_s`; zeroes the args struct safely. |
| `make_dev_s(args, &cdev, fmt, ...)` | The modern preferred form. Returns 0 or errno. |
| `make_dev(devsw, unit, uid, gid, mode, fmt, ...)` | Older single-call form. Discouraged for new code. |
| `make_dev_credf(flags, ...)` | When you need `MAKEDEV_*` flag bits. |
| `destroy_dev(cdev)` | Always in detach; drains in-flight callbacks. |
| `destroy_dev_drain(cdev)` | When detach must wait for outstanding refs. |

### Quick Reference: cdevsw Flags

| Flag | Meaning |
|------|---------|
| `D_VERSION` | Required; identifies the cdevsw layout version. |
| `D_TRACKCLOSE` | Call `d_close` only on the last close. Recommended. |
| `D_NEEDGIANT` | Take the giant lock around every callback. Discouraged. |
| `D_DISK` | Cdev represents a disk; uses bio rather than uio for I/O. |
| `D_TTY` | Cdev is a terminal; affects line discipline routing. |
| `D_MMAP_ANON` | Cdev supports anonymous mmap. |
| `D_MEM` | Cdev is `/dev/mem`-like; raw memory access. |

### Quick Reference: `make_dev` Flags (`MAKEDEV_*`)

| Flag | Meaning |
|------|---------|
| `MAKEDEV_REF` | Take an extra reference; caller must `dev_rel` later. |
| `MAKEDEV_NOWAIT` | Do not sleep; return `ENOMEM` if no memory. |
| `MAKEDEV_WAITOK` | Sleeping is allowed (default for most callers). |
| `MAKEDEV_ETERNAL` | Cdev never goes away; certain optimisations apply. |
| `MAKEDEV_ETERNAL_KLD` | Like ETERNAL, but only for the lifetime of the kld. |
| `MAKEDEV_CHECKNAME` | Validate the name; `ENAMETOOLONG` if too long. |

### Quick Reference: ioctl Encoding Macros

All in `/usr/src/sys/sys/ioccom.h`.

| Macro | Direction | Argument |
|-------|-----------|----------|
| `_IO(g, n)` | none | none |
| `_IOR(g, n, t)` | out | type `t`, size `sizeof(t)` |
| `_IOW(g, n, t)` | in | type `t`, size `sizeof(t)` |
| `_IOWR(g, n, t)` | in and out | type `t`, size `sizeof(t)` |
| `_IOWINT(g, n)` | none, but value of int passed | int |

The arguments mean:

* `g`: group letter, conventionally `'M'` for `myfirst` and similar.
* `n`: command number, monotonically increasing within the group.
* `t`: argument type, used only for its `sizeof`.

The maximum size is `IOCPARM_MAX = 8192` bytes. For larger transfers, use the embedded-pointer pattern (Challenge 1) or a different mechanism such as `mmap` or `read`/`write`.

### Quick Reference: `d_ioctl_t` Signature

```c
int d_ioctl(struct cdev *dev, u_long cmd, caddr_t data,
            int fflag, struct thread *td);
```

| Argument | Meaning |
|----------|---------|
| `dev` | The cdev backing the file descriptor. Use `dev->si_drv1` to get the softc. |
| `cmd` | The request code from user space. Compared to named constants. |
| `data` | Kernel-side buffer with the user data. Direct dereference; no `copyin` needed. |
| `fflag` | File flags from the open call (`FREAD`, `FWRITE`). Check before mutating. |
| `td` | Calling thread. Use `td->td_ucred` for credentials. |

Return 0 on success, a positive errno on failure, or `ENOIOCTL` for unknown commands.

### Quick Reference: sysctl OID Macros

All in `/usr/src/sys/sys/sysctl.h`.

| Macro | Adds |
|-------|------|
| `SYSCTL_ADD_INT(ctx, parent, nbr, name, flags, ptr, val, descr)` | Signed int, backed by `*ptr`. |
| `SYSCTL_ADD_UINT` | Unsigned int. |
| `SYSCTL_ADD_LONG` / `SYSCTL_ADD_ULONG` | Long / unsigned long. |
| `SYSCTL_ADD_S64` / `SYSCTL_ADD_U64` | 64-bit signed / unsigned. |
| `SYSCTL_ADD_BOOL` | Boolean (preferred over int 0/1). |
| `SYSCTL_ADD_STRING(ctx, parent, nbr, name, flags, ptr, len, descr)` | NUL-terminated string. |
| `SYSCTL_ADD_NODE(ctx, parent, nbr, name, flags, handler, descr)` | Subtree node. |
| `SYSCTL_ADD_PROC(ctx, parent, nbr, name, flags, arg1, arg2, handler, fmt, descr)` | Handler-backed OID. |

### Quick Reference: sysctl Flag Bits

| Flag | Meaning |
|------|---------|
| `CTLFLAG_RD` | Read-only. |
| `CTLFLAG_WR` | Write-only (rare). |
| `CTLFLAG_RW` | Read-write. |
| `CTLFLAG_TUN` | Loader tunable; read at boot from `/boot/loader.conf`. |
| `CTLFLAG_MPSAFE` | Handler is safe without giant lock. **Always set for new code.** |
| `CTLFLAG_PRISON` | Visible inside jails. |
| `CTLFLAG_VNET` | Per-VNET (virtualised network stack). |
| `CTLFLAG_DYN` | Dynamic OID; set automatically by `SYSCTL_ADD_*`. |
| `CTLFLAG_SECURE` | Read-only when `securelevel > 0`. |

### Quick Reference: sysctl Type Bits

OR'd into the flag word for `SYSCTL_ADD_PROC` and similar.

| Flag | Meaning |
|------|---------|
| `CTLTYPE_INT` / `CTLTYPE_UINT` | Signed / unsigned int. |
| `CTLTYPE_LONG` / `CTLTYPE_ULONG` | Long / unsigned long. |
| `CTLTYPE_S64` / `CTLTYPE_U64` | 64-bit signed / unsigned. |
| `CTLTYPE_STRING` | NUL-terminated string. |
| `CTLTYPE_OPAQUE` | Opaque blob; rarely used in new code. |
| `CTLTYPE_NODE` | Subtree node. |

### Quick Reference: sysctl Handler Format Strings

Used by `SYSCTL_ADD_PROC` to tell `sysctl(8)` how to print the value.

| Format | Type |
|--------|------|
| `"I"` | int |
| `"IU"` | unsigned int |
| `"L"` | long |
| `"LU"` | unsigned long |
| `"Q"` | int64 |
| `"QU"` | uint64 |
| `"A"` | NUL-terminated string |
| `"S,structname"` | opaque struct (rare) |

### Quick Reference: sysctl Handler Boilerplate

```c
static int
my_handler(SYSCTL_HANDLER_ARGS)
{
        struct my_softc *sc = arg1;
        u_int val;

        /* Read the current value into val under the mutex. */
        mtx_lock(&sc->sc_mtx);
        val = sc->sc_field;
        mtx_unlock(&sc->sc_mtx);

        /* Let the framework do the read or write. */
        return (sysctl_handle_int(oidp, &val, 0, req));
}
```

### Quick Reference: per-Device Sysctl Context

```c
struct sysctl_ctx_list *ctx = device_get_sysctl_ctx(dev);
struct sysctl_oid      *tree = device_get_sysctl_tree(dev);
struct sysctl_oid_list *child = SYSCTL_CHILDREN(tree);
```

The framework owns the ctx; the driver does not call `sysctl_ctx_free` on it. The framework cleans up automatically after a successful detach.

### Quick Reference: Loader Tunables

```c
TUNABLE_INT_FETCH("hw.driver.knob", &sc->sc_knob);
TUNABLE_LONG_FETCH("hw.driver.knob", &sc->sc_knob);
TUNABLE_STR_FETCH("hw.driver.knob", buf, sizeof(buf));
```

The first argument is the loader variable name. The second is a pointer to the destination, which is also the default value if the variable is not set.

### Quick Reference: ifnet Lifecycle

| Function | When |
|----------|------|
| `if_alloc(IFT_<type>)` | Allocate the ifnet. |
| `if_initname(ifp, name, unit)` | Set the user-visible name (`ifconfig` shows it). |
| `if_setflags(ifp, flags)` | Set `IFF_*` flags. |
| `if_setsoftc(ifp, sc)` | Attach the driver's softc. |
| `if_setioctlfn(ifp, fn)` | Set the ioctl handler. |
| `if_settransmitfn(ifp, fn)` | Set the transmit function. |
| `if_attach(ifp)` | Make the interface visible. |
| `bpfattach(ifp, dlt, hdrlen)` | Make traffic visible to BPF. |
| `bpfdetach(ifp)` | Reverse `bpfattach`. |
| `if_detach(ifp)` | Reverse `if_attach`. |
| `if_free(ifp)` | Free the ifnet. |

### Quick Reference: CAM SIM Lifecycle

| Function | When |
|----------|------|
| `cam_simq_alloc(maxq)` | Allocate the device queue. |
| `cam_sim_alloc(action, poll, name, sc, unit, mtx, max_tagged, max_dev_tx, devq)` | Allocate the SIM. |
| `xpt_bus_register(sim, dev, 0)` | Register the bus with CAM. |
| `xpt_create_path(&path, NULL, cam_sim_path(sim), targ, lun)` | Create a path for events. |
| `xpt_action(ccb)` | Send a CCB to CAM. |
| `xpt_done(ccb)` | Tell CAM that the driver finished a CCB. |
| `xpt_free_path(path)` | Reverse `xpt_create_path`. |
| `xpt_bus_deregister(cam_sim_path(sim))` | Reverse `xpt_bus_register`. |
| `cam_sim_free(sim, free_devq)` | Reverse `cam_sim_alloc`. Pass `TRUE` to also free the devq. |

### Quick Reference: Module Lifecycle

```c
static moduledata_t mymod = {
        "myfirst",        /* name */
        myfirst_modevent, /* event handler, can be NULL */
        NULL              /* extra data, rarely used */
};
DECLARE_MODULE(myfirst, mymod, SI_SUB_DRIVERS, SI_ORDER_ANY);
MODULE_VERSION(myfirst, 1);
MODULE_DEPEND(myfirst, otherdriver, 1, 1, 1);
```

The event handler signature is `int (*)(module_t mod, int what, void *arg)`. The `what` argument is one of `MOD_LOAD`, `MOD_UNLOAD`, `MOD_QUIESCE`, or `MOD_SHUTDOWN`. Return 0 on success or a positive errno.

### Quick Reference: Event Handler

```c
eventhandler_tag tag;

tag = EVENTHANDLER_REGISTER(event_name, callback,
    arg, EVENTHANDLER_PRI_ANY);

EVENTHANDLER_DEREGISTER(event_name, tag);
```

Common event names: `shutdown_pre_sync`, `shutdown_post_sync`, `shutdown_final`, `vm_lowmem`, `power_suspend_early`, `power_resume`.

### Quick Reference: Errno Conventions

| Errno | When to return |
|-------|----------------|
| `0` | Success. |
| `EINVAL` | The arguments are recognised but invalid. |
| `EBADF` | The file descriptor is not opened correctly for the operation. |
| `EBUSY` | The resource is in use (often returned from detach). |
| `ENOIOCTL` | The ioctl command is unknown. **Use this for the default case in `d_ioctl`.** |
| `ENOTTY` | The kernel translation of `ENOIOCTL` for user space. |
| `ENOMEM` | Allocation failed. |
| `EAGAIN` | Try again later (often returned from non-blocking I/O). |
| `EPERM` | The caller lacks the necessary privilege. |
| `EOPNOTSUPP` | The operation is not supported by this driver. |
| `EFAULT` | A user pointer is invalid (returned by failed `copyin`/`copyout`). |
| `ETIMEDOUT` | A wait timed out. |
| `EIO` | Generic I/O error from hardware. |

### Quick Reference: Debug Class Bits (from Chapter 23)

| Bit | Name | Used for |
|-----|------|----------|
| `0x01` | `MYF_DBG_INIT` | probe / attach / detach |
| `0x02` | `MYF_DBG_OPEN` | open / close lifecycle |
| `0x04` | `MYF_DBG_IO` | read / write paths |
| `0x08` | `MYF_DBG_IOCTL` | ioctl handling |
| `0x10` | `MYF_DBG_INTR` | interrupt handler |
| `0x20` | `MYF_DBG_DMA` | DMA mapping/sync |
| `0x40` | `MYF_DBG_PWR` | power-management events |
| `0x80` | `MYF_DBG_MEM` | malloc/free trace |
| `0xFFFFFFFF` | `MYF_DBG_ANY` | all classes |
| `0` | `MYF_DBG_NONE` | no logging |

The mask is set per instance via `dev.<driver>.<unit>.debug.mask`, or globally at boot via `hw.<driver>.debug_mask_default` in `/boot/loader.conf`.

### Glossary of Integration Vocabulary

**API version**: An integer exposed through a driver's ioctl interface (typically through a `GETVER` ioctl) that user-space programs can query to detect changes in the driver's public interface. Bumped only when the user-visible interface changes incompatibly.

**bpfattach**: The function that hooks an `ifnet` into the BPF (Berkeley Packet Filter) machinery so `tcpdump` and similar tools can observe its traffic. Must be paired with `bpfdetach`.

**bus_generic_detach**: A helper that detaches every child of a bus-style driver. Used as the first step in a bus driver's detach to release child devices before the parent tears down its own state.

**CAM**: The Common Access Method, FreeBSD's storage subsystem above device drivers. Owns the I/O queue, target/LUN abstraction, and peripheral drivers (`da`, `cd`, `sa`).

**CCB**: CAM Control Block. A single I/O request structured as a tagged union; the driver inspects `ccb->ccb_h.func_code` and dispatches accordingly. Completed via `xpt_done`.

**cdev**: Character device. The kernel's per-device-node object that backs an entry under `/dev`. Created with `make_dev_s`, destroyed with `destroy_dev`.

**cdevsw**: Character device switch. The static table of callbacks (`d_open`, `d_close`, `d_read`, `d_write`, `d_ioctl`, ...) that the kernel invokes on operations against a cdev.

**copyin / copyout**: Functions that transfer bytes between user-space and kernel-space addresses. The kernel performs them automatically for properly encoded ioctls; the driver calls them explicitly only for embedded-pointer patterns.

**CTLFLAG_MPSAFE**: A sysctl flag that declares the OID's handler safe to call without the giant lock. Mandatory for new code; without it the kernel takes the giant lock around every access.

**d_ioctl_t**: The function-pointer type for the cdevsw's ioctl callback. Signature: `int (*)(struct cdev *, u_long, caddr_t, int, struct thread *)`.

**d_priv**: A per-file-descriptor private pointer attached via `devfs_set_cdevpriv`. Used for state that must be associated with one open rather than with the driver instance as a whole.

**dev_ref / dev_rel**: A pair of functions that increment and decrement a cdev's reference count. Used to coordinate detach with in-flight callbacks; see Challenge 3.

**devfs**: The kernel-managed file system that backs `/dev`. The driver creates cdevs and devfs makes them visible.

**devfs.rules(8)**: A configuration mechanism for runtime devfs permissions. Applied by `service devfs restart` after editing `/etc/devfs.rules`.

**DTrace**: The dynamic tracing framework. Drivers expose probe points through SDT macros; DTrace scripts attach to them at runtime.

**EVENTHANDLER_REGISTER**: The mechanism for registering a callback for a named system-wide event (`shutdown_pre_sync`, `vm_lowmem`, etc.). Must be paired with `EVENTHANDLER_DEREGISTER`.

**ifnet**: The network stack's per-interface object. The network counterpart to the cdev.

**if_t**: The opaque typedef the network stack uses for `ifnet`. Drivers manipulate the interface through accessor functions rather than direct field access.

**IOC_VOID / IOC_IN / IOC_OUT / IOC_INOUT**: The four direction bits encoded in an ioctl request code. Used by the kernel to decide what `copyin`/`copyout` to perform.

**IOCPARM_MAX**: The maximum size (8192 bytes) of an ioctl's argument structure as encoded in the request code. Larger transfers require an embedded-pointer pattern.

**kldload / kldunload**: The user-space tools that load and unload kernel modules. Both invoke the corresponding module event handler (`MOD_LOAD` and `MOD_UNLOAD`).

**make_dev_args**: The structure passed to `make_dev_s` to describe a new cdev. Initialised with `make_dev_args_init`.

**make_dev_s**: The modern preferred function for creating a cdev. Returns 0 or a positive errno; sets `*cdev` on success.

**MAKEDEV_***: Flag bits passed to `make_dev_credf` and similar. Common bits: `MAKEDEV_REF`, `MAKEDEV_NOWAIT`, `MAKEDEV_ETERNAL_KLD`.

**MOD_LOAD / MOD_UNLOAD / MOD_SHUTDOWN**: The events delivered to a module's event handler. Returns 0 to acknowledge or non-zero to reject.

**MODULE_DEPEND**: The macro that declares a module's dependency on another. The kernel uses the version arguments (`min`, `pref`, `max`) to enforce compatibility.

**MODULE_VERSION**: The macro that declares a module's version number. Bumped when in-kernel users would need a recompile.

**Newbus**: FreeBSD's device-tree framework. Owns the `device_t`, the per-device softc, the per-device sysctl context, and the probe/attach/detach lifecycle.

**OID**: Object identifier. A node in the sysctl tree. Static OIDs are declared at compile time; dynamic OIDs are added at runtime with `SYSCTL_ADD_*`.

**Path (CAM)**: A `(bus, target, LUN)` triple identifying a CAM destination. Created with `xpt_create_path`.

**Public header**: A header that user-space programs include to talk to the driver. Must compile cleanly without `_KERNEL` defined; uses only stable types.

**Registration framework**: A FreeBSD subsystem that exposes an "allocate-name-fill-attach-traffic-detach-free" interface for drivers. Examples: networking (`ifnet`), storage (CAM), sound, USB, GEOM.

**Release version**: The human-readable string identifying a driver's release. Exposed through sysctl as `dev.<driver>.<unit>.version`.

**SDT**: Statically defined tracing. The kernel's mechanism for compile-time probe points consumable by DTrace.

**si_drv1 / si_drv2**: Two private pointer fields in `struct cdev` available for the driver's use. Conventionally `si_drv1` points at the softc.

**SIM**: SCSI Interface Module. CAM's view of a storage adapter. Allocated with `cam_sim_alloc`, registered with `xpt_bus_register`.

**Soft detach**: A detach pattern where the driver waits for outstanding references to drop to zero rather than refusing detach immediately. See Challenge 3.

**Softc**: Software context. The driver's per-instance state. Allocated by Newbus and accessed via `device_get_softc(dev)`.

**SYSINIT**: A compile-time-registered one-shot kernel init function. Runs at a specific stage during boot. Rarely needed in driver code.

**SYSCTL_HANDLER_ARGS**: A macro that expands to the standard argument list for a sysctl handler: `oidp, arg1, arg2, req`.

**TUNABLE_INT_FETCH**: A function that reads a value from the loader environment and writes it into a kernel variable. The variable retains its previous value if the loader variable is absent.

**XPT**: CAM Transport Layer. The central CAM dispatch mechanism. Driver calls `xpt_action` to send a CCB; CAM calls back via the SIM's action function for I/O CCBs.

### Companion File Inventory

The companion files for this chapter live under `examples/part-05/ch24-integration/` in the book's repository. The directory layout is:

```text
examples/part-05/ch24-integration/
├── README.md
├── INTEGRATION.md
├── stage1-devfs/
│   ├── Makefile
│   ├── myfirst.c             (with make_dev_args)
│   └── README.md
├── stage2-ioctl/
│   ├── Makefile
│   ├── Makefile.user         (for myfirstctl)
│   ├── myfirst_ioctl.c
│   ├── myfirst_ioctl.h       (PUBLIC)
│   ├── myfirstctl.c
│   └── README.md
├── stage3-sysctl/
│   ├── Makefile
│   ├── myfirst.c
│   ├── myfirst_sysctl.c
│   └── README.md
└── labs/
    ├── lab24_1_stage1.sh     (verification commands for Lab 1)
    ├── lab24_2_stage2.sh
    ├── lab24_3_stage3.sh
    ├── lab24_4_failure.sh
    ├── lab24_5_dtrace.sh
    ├── lab24_6_smoke.sh
    ├── lab24_7_reload.sh
    └── loader.conf.example
```

The starting point for Lab 1 is the reader's own end-of-Chapter-23 driver (`1.6-debug`); `stage1-devfs/`, `stage2-ioctl/`, and `stage3-sysctl/` are reference solutions the reader can consult after completing each lab. The labs directory has small shell scripts that perform the verification commands and that the reader can adapt for their own tests.

The `README.md` at the chapter root describes how to use the directory, the order of the stages, and the relationship between the staged trees. The `INTEGRATION.md` is a longer document that maps every concept in the chapter to the file it appears in.

### Where the Chapter's Source Tree Sits in Real FreeBSD

For a reader who wants to look up the in-tree implementations referenced throughout the chapter, here is a short index of the most important files:

| Concept | In-tree file |
|---------|--------------|
| ioctl encoding | `/usr/src/sys/sys/ioccom.h` |
| cdevsw definition | `/usr/src/sys/sys/conf.h` |
| make_dev family | `/usr/src/sys/kern/kern_conf.c` |
| sysctl framework | `/usr/src/sys/sys/sysctl.h`, `/usr/src/sys/kern/kern_sysctl.c` |
| ifnet API | `/usr/src/sys/net/if.h`, `/usr/src/sys/net/if.c` |
| ifnet example | `/usr/src/sys/net/if_disc.c` |
| CAM SIM API | `/usr/src/sys/cam/cam_xpt.h`, `/usr/src/sys/cam/cam_sim.h` |
| CAM example | `/usr/src/sys/dev/ahci/ahci.c` |
| EVENTHANDLER | `/usr/src/sys/sys/eventhandler.h` |
| MODULE machinery | `/usr/src/sys/sys/module.h`, `/usr/src/sys/kern/kern_module.c` |
| TUNABLE | `/usr/src/sys/sys/sysctl.h` (search for `TUNABLE_INT_FETCH`) |
| SDT probes | `/usr/src/sys/sys/sdt.h`, `/usr/src/sys/cddl/dev/sdt/sdt.c` |
| The `null(4)` reference | `/usr/src/sys/dev/null/null.c` |

Reading these files alongside the chapter is the next step for any reader who wants to deepen their integration knowledge. The `null(4)` driver in particular is worth reading in full; it is small enough to absorb in one sitting and demonstrates almost every pattern this chapter covered.

### What Did Not Make It Into the Chapter

A handful of integration topics belong to FreeBSD's wider toolbox but did not earn a section here, either because they are subsystem-specific in a way that would distract a beginner or because they are covered more thoroughly in a later chapter. Naming them here keeps the chapter honest about its scope and gives the reader a forward-looking map.

The first omission is `geom(4)`. A driver that exposes a block device hooks into GEOM rather than CAM. The registration pattern is similar to the cdev pattern (allocate a `g_geom`, fill in `g_class` callbacks, call `g_attach`), but the vocabulary is different enough that mixing it into the chapter would have blurred the storage-versus-character distinction. Drivers for raw disks and pseudo-disk targets live in this neighbourhood; the canonical reference is `/usr/src/sys/geom/geom_disk.c`.

The second omission is `usb(4)`. A USB driver registers with the USB stack through `usb_attach` and a USB-specific method table rather than through Newbus directly. The integration surfaces (devfs, sysctl) are the same once the device is attached, but the upper edge is owned by the USB stack. The canonical references are under `/usr/src/sys/dev/usb/`.

The third omission is `iicbus(4)` and `spibus(4)`. Drivers that talk to I2C or SPI peripherals attach as children of a bus driver and use bus-specific transfer routines. The integration surfaces remain the same, but the device-tree and FDT integration that drives modern Arm SoCs adds vocabulary that warrants its own chapter. Part VI covers these surfaces in their proper context.

The fourth omission is the `kqueue(2)` and `poll(2)` integration. A character driver that wants to wake user-space programs blocked on `select`, `poll`, or `kqueue` must implement `d_kqfilter` (and optionally `d_poll`), wire `selwakeup` and `KNOTE` into the data path, and supply a small set of filter operations. The mechanism is not difficult, but it is conceptually a layer above the basic cdev contract; we will return to it in Chapter 26.

A reader who needs any of these surfaces today should treat the chapter's pattern as the foundation and reach for the in-tree references named above. The discipline (register at attach, drain at detach, hold a single mutex across mutating callbacks, version the public surface) is the same.

### Reader Self-Check

Before turning the page, the reader who has worked through the chapter should be able to answer the following questions without consulting the text. Each question maps to a section that introduced the underlying material. If a question is unfamiliar, the chapter section listed in parentheses is the right place to revisit before moving on.

1. What does `D_TRACKCLOSE` change about how `d_close` is invoked? (Section 2)
2. Why is `mda_si_drv1` preferable to assigning `si_drv1` after `make_dev` returns? (Section 2)
3. What does the `_IOR('M', 1, uint32_t)` macro encode in the resulting request code? (Section 3)
4. Why must the dispatcher's default branch return `ENOIOCTL` rather than `EINVAL`? (Section 3)
5. Which kernel function tears down the per-device sysctl context, and when does it run? (Section 4)
6. How does `CTLFLAG_TUN` cooperate with `TUNABLE_INT_FETCH` to apply a boot-time value? (Section 4)
7. What is the difference between the `MYFIRST_VERSION` string, the `MODULE_VERSION` integer, and the `MYFIRST_IOCTL_VERSION` integer? (Section 8)
8. Why does the cleanup chain in attach use labelled `goto`s in reverse order rather than nested `if` statements? (Section 7)
9. How does the chapter's soft-detach pattern differ from the `dev_ref`/`dev_rel` pattern that Challenge 3 sketches? (Sections 7 and 10)
10. Which two integration surfaces are required for almost every driver, and which two are required only for drivers that join a specific subsystem? (Sections 1, 2, 3, 4, 5, 6)

A reader who answers most of these without hesitation has internalised the chapter and is ready for what comes next. A reader who hesitates on more than two should revisit the relevant sections before tackling Chapter 25's maintenance discipline.

### Final Word

Integration is what turns a working module into a usable driver. The patterns in this chapter are not optional polish; they are the difference between a driver that an operator can adopt and one that an operator has to fight. Master them once, and every subsequent driver gets easier to build, easier to maintain, and easier to ship.

The next chapter takes the discipline introduced here and generalises it into a set of maintenance habits: rate-limited logging, consistent errno vocabulary, tunables and versioning, production-grade cleanup, and the `SYSINIT`/`SYSUNINIT`/`EVENTHANDLER` mechanisms that extend a driver's lifecycle beyond simple load and unload. The vocabulary changes, but the rhythm is the same: register, take traffic, deregister cleanly. With the foundation of Chapter 24 in place, Chapter 25 will feel like a natural extension rather than a new world.

One last thought before turning the page. The integration surfaces in this chapter are deliberately small in number. There are devfs, ioctl, and sysctl. There are the optional registrations to subsystems like ifnet and CAM. There is the lifecycle discipline that ties them together. Five concepts, in total.

Once these are familiar, the rest of the book is the application of the same patterns to ever more interesting hardware. A reader who has finished this chapter has finished the kernel-API half of the book; what remains is the systems and the discipline to wield it well.

