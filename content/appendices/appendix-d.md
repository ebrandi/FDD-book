---
title: "Operating System Concepts"
description: "A concept-level companion to the kernel/user boundary, driver types, recurring kernel data structures, and the boot-to-init path that drivers must fit into."
appendix: "D"
lastUpdated: "2026-04-20"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "TBD"
estimatedReadTime: 25
---

# Appendix D: Operating System Concepts

## How to Use This Appendix

The main chapters teach the reader how to write a FreeBSD device driver: how to declare a softc, how to probe and attach to a bus, how to register a character device, how to set up an interrupt, how to do DMA. Beneath all of that there is a running assumption that the reader has a working mental model of what an operating system kernel actually is, how it differs from userland, what a driver is *inside* that kernel, and how the machine gets from power-on to a kernel that is ready to load and run that driver. The book introduces each piece of this model in the chapter where it first becomes relevant, but the pieces never appear in one place together.

This appendix is that one place. It is a concept companion for the operating-system ideas the rest of the book keeps reusing: the kernel/user boundary, the definition of a driver, the handful of data structures you will see again and again in FreeBSD source, and the boot-to-init sequence that sets the stage for everything a driver can do. It is deliberately short. The goal is to consolidate the mental model, not to teach operating-systems theory, and not to repeat what the chapters already teach at length.

### What You Will Find Here

The appendix is organised by driver relevance, not by textbook taxonomy. Four sections, each with a small set of entries. Each entry follows the same compact rhythm:

- **What it is.** One or two sentences of plain definition.
- **Why this matters to driver writers.** The concrete place where the concept shows up in your code.
- **How this shows up in FreeBSD.** The API, header, subsystem, or convention that names the idea.
- **Common trap.** The misunderstanding that actually costs people time.
- **Where the book teaches this.** A pointer back to the chapter that uses it in context.
- **What to read next.** A manual page, a header, or a real source file you can open.

Not every entry uses every label; the shape is a guide, not a template.

### What This Appendix Is Not

It is not an introduction to operating-systems theory. If you have never met the idea of a process or an address space before, pick up a general operating-systems textbook first and come back. It is not a systems-programming tutorial either; the early chapters of the book already handle that. And it does not overlap with the other appendices. Appendix A is the API lookup. Appendix B is the algorithmic-pattern field guide. Appendix C is the hardware-concepts companion. Appendix E is the kernel-subsystem reference. If the question you want answered is "what does this macro call do", "what data structure should I use", "what is a BAR", or "how does the scheduler work", you want a different appendix. This one keeps its focus on the operating-system mental model that makes driver work make sense.

## Reader Guidance

Three ways to use this appendix, each with a different reading strategy.

If you are **learning the main chapters**, keep this appendix open as a companion. When Chapter 3 discusses the UNIX division between the kernel and userland, glance at Section 1 here for the driver-relevant summary. When Chapter 6 introduces Newbus and the driver lifecycle, Section 2 compresses the same story into a one-page mental model. When Chapter 11 walks you through your first mutex, Section 3 names the family. A first pass through the whole appendix should take about twenty-five minutes; daily use looks more like two or three minutes at a time.

If you are **reading unfamiliar kernel code**, treat the appendix as a translator. Kernel source assumes the reader already knows why `copyin(9)` exists, what a softc is, when a `TAILQ` is better than a `LIST`, and where `SYSINIT` chains come from. If any of those words feel fuzzy when you meet them in code, the entry here names the concept in one page and points to the chapter that teaches it in full.

If you are **coming back to the book after time away**, read the appendix as a consolidation. The concepts here are the recurring spine of driver work. Re-reading them is a low-cost way to reload the mental model before you open an unfamiliar driver.

A few conventions apply throughout:

- Source paths are shown in the book-facing form, `/usr/src/sys/...`, matching a standard FreeBSD system. You can open any of them on your lab machine.
- Manual pages are cited in the usual FreeBSD style. Kernel-facing pages live in section 9 (`intr_event(9)`, `mtx(9)`, `kld(9)`). Userland system calls live in section 2 (`open(2)`, `ioctl(2)`). Device overviews live in section 4 (`devfs(4)`, `pci(4)`).
- When an entry points to real source as reading material, the file is one a beginner can navigate in a single sitting.

With that framing in place, we start where every driver must start: by understanding which side of the kernel/user line it lives on.

## Section 1: What Is a Kernel, and What Does It Do?

A kernel is the program that owns the machine. It is the only program that runs in the CPU's most privileged mode, the only program that may touch physical memory directly, the only program that may talk to hardware, and the only program that may decide which other programs run and when. Everything else (shells, compilers, web browsers, daemons, your own programs) runs in a less privileged mode and asks the kernel for the things it cannot do itself. A driver is kernel code. That single fact changes the rules the code must follow, and the rest of this section explains why.

### Kernel Responsibilities in One Page

**What it is.** The FreeBSD kernel is responsible for a small, well-defined set of jobs: managing the CPU (processes, threads, scheduling, interrupts), managing memory (virtual address spaces, page tables, allocation), managing I/O (talking to devices through drivers), managing files and filesystems (through VFS and GEOM), and managing the network stack. It also mediates security boundaries, handles signals, and serves as the only entry point for every system call.

**Why this matters to driver writers.** A driver is not a free-standing program. It is a participant in each of those jobs. A network driver participates in the networking subsystem. A storage driver participates in the I/O and filesystem subsystems. A sensor driver participates in the I/O subsystem and sometimes in the event-handler subsystem. When you write a driver, you are extending the kernel, not sitting beside it. The kernel's invariants become your invariants, and every convention the kernel relies on (locking discipline, memory discipline, error-return conventions, cleanup ordering) becomes a convention you must honour.

**How this shows up in FreeBSD.** The kernel source tree under `/usr/src/sys/` is organised by responsibility: `/usr/src/sys/kern/` holds the machine-independent core (process management, scheduling, locking, VFS), `/usr/src/sys/vm/` holds the virtual-memory system, `/usr/src/sys/dev/` holds the device drivers, `/usr/src/sys/net/` and `/usr/src/sys/netinet/` hold the network stack, and `/usr/src/sys/amd64/` (or `arm64/`, `i386/`, `riscv/`) holds the machine-dependent code. When you read a driver, you are reading code that ties together pieces from several of these directories.

**Common trap.** Treating the kernel as a passive library that drivers "call into". The kernel is the running program; your driver lives inside it. A crash, a memory leak, or a held lock in your driver is a crash, a memory leak, or a held lock for the whole system.

**Where the book teaches this.** Chapter 3 introduces the UNIX and FreeBSD picture. Chapter 5 sharpens the distinction at the level of kernel-space C. Chapter 6 grounds it in driver anatomy.

**What to read next.** `intro(9)` for the top-level kernel overview, and the directory listing of `/usr/src/sys/` itself. Spend five minutes reading folder names and you will already have a rough map.

### Kernel Space vs User Space

**What it is.** Two distinct execution environments inside the same machine. Kernel space runs with full CPU privileges, shares one address space among all kernel threads, and has direct access to hardware. User space runs with reduced privileges, lives in per-process virtual address spaces that the kernel sets up and protects, and has no direct access to hardware. A single machine is always running code from both environments; the CPU switches between them many times per second.

**Why this matters to driver writers.** Almost every rule that makes kernel programming feel different from user programming comes from this separation. A kernel pointer is not addressable from user space, and a user pointer is not safely addressable from kernel space. User memory may be swapped, paged, or unmapped at any moment; kernel memory is stable within its allocation lifetime. The kernel must never blindly dereference a pointer that came from userland; it must always use the dedicated copy primitives. And because one kernel address space is shared by every thread in the kernel, any kernel bug that stomps memory can corrupt every subsystem at once.

**How this shows up in FreeBSD.** Three places in driver code are visible every time the boundary is crossed:

- `copyin(9)` and `copyout(9)` move fixed-size data between a user buffer and a kernel buffer, returning an error on a bad user address instead of panicking.
- `copyinstr(9)` does the same for NUL-terminated strings with a caller-supplied length cap.
- `uiomove(9)` walks a `struct uio` (user I/O descriptor) through either direction of a `read` or `write` system call, handling the buffer list and the direction flag for you.

Under the hood, each of these routines uses machine-dependent helpers that know how to catch a fault from a bad user address and convert it into an `EFAULT` return rather than a kernel panic.

**Common trap.** Dereferencing a user pointer directly inside a syscall handler or an `ioctl` implementation. The code may appear to work when the user passes a valid pointer and the page happens to be resident. It will panic or silently misbehave the moment the user passes something bad (either by mistake or deliberately), and that is one of the easier bugs to turn into a security problem.

**Where the book teaches this.** Chapter 3 sets up the distinction. Chapter 5 covers it in kernel C. Chapter 9 walks the boundary in character-driver code.

**What to read next.** `copyin(9)`, `copyout(9)`, `uiomove(9)`, and the header `/usr/src/sys/sys/uio.h` for `struct uio`.

### System Calls: The Door Between the Two Sides

**What it is.** A system call is the disciplined mechanism a user-space program uses to ask the kernel to do something on its behalf. The user program invokes a special instruction (`syscall` on amd64, `svc` on arm64, with equivalent traps on every other architecture FreeBSD supports), which causes the CPU to trap into kernel mode, switch to the kernel stack of the current thread, and dispatch to the implementation the kernel has registered for that system-call number.

**Why this matters to driver writers.** Most driver interaction with userland happens because a userland program issued a system call. When the user runs `cat /dev/mydevice`, `cat(1)` invokes `open(2)`, `read(2)`, and `close(2)` against that device node. Each of those system calls eventually reaches your driver through the `cdev` switch table you registered with `make_dev_s(9)`: `open(2)` becomes `d_open`, `read(2)` becomes `d_read`, `ioctl(2)` becomes `d_ioctl`. Your driver's entry points are therefore system-call entry points in disguise, which is exactly why the copy primitives above matter so much.

**How this shows up in FreeBSD.** Two pieces. First, the system-call table itself and the trap-handling path, which live in the machine-independent core and in the architecture-specific backends. Second, the driver-visible structures that a system call hands you: a `struct thread *` for the calling thread, a `struct uio *` for the buffer list on `read`/`write`, an argument pointer for `ioctl`, and a set of flags that describe the mode of the call. The driver does not see the trap; it sees a function call that arrives already in kernel context.

**Common trap.** Assuming the calling thread can be treated as an ordinary kernel thread. A thread that entered the kernel through a system call is temporarily running kernel code on behalf of a user process. It may be killed if the process is killed, its priority may be different from an internal kernel thread, and it is usually the wrong thread to own long-lived work. Long work belongs on a taskqueue or a kernel thread the driver owns.

**Where the book teaches this.** Chapter 3 names the mechanism. Chapter 9 exercises it in a working read/write path.

**What to read next.** `syscall(2)` for the userland view, `uiomove(9)` for the kernel entry shape, and `/usr/src/sys/kern/syscalls.master` if you are curious about the actual system-call numbering table.

### Why Kernel Mistakes Have System-Wide Consequences

**What it is.** A short way of stating what the previous entries imply. A kernel thread can read and write any kernel memory; there is no per-module address space inside the kernel. A kernel bug is therefore free to damage any data structure, including data structures that belong to other subsystems. A kernel crash takes the whole machine down. A kernel deadlock can wedge threads across every driver at once.

**Why this matters to driver writers.** The blast radius of a kernel bug is the whole system. That is why the book repeats the same disciplines in every chapter: balanced allocate/free pairs, balanced lock acquire/release pairs, consistent cleanup ladders on error, defensive validation of any pointer that comes from userland, and careful ordering at boundaries (interrupts, DMA, device registers). None of these disciplines are about heroics; they are about acknowledging that the cost of a kernel mistake is paid by every process on the machine, not only by the driver that made it.

**How this shows up in FreeBSD.** Directly, in the tools the kernel offers you to catch mistakes before they escape. `witness(9)` verifies lock ordering. `KASSERT(9)` lets you encode invariants that compile away in production but fire loudly in a development build. `INVARIANTS` and `INVARIANT_SUPPORT` in the kernel config turn on extra self-checks. DTrace lets you watch driver behaviour under real load. Your development lab should always be running a kernel with these diagnostics enabled; your production target should not.

**Common trap.** Testing only on a release-build kernel and discovering the bugs in production. The development kernel is the one that finds the problem cheaply. Use it.

**Where the book teaches this.** Chapter 2 walks you through building a diagnostics-enabled lab kernel. Chapter 5 makes the discipline concrete at the C level. Every chapter from Chapter 11 onward depends on these habits.

**What to read next.** `witness(9)`, `kassert(9)`, `ddb(4)`, and the kernel-debugging options in `/usr/src/sys/conf/NOTES`.

### How Drivers Participate in Kernel Responsibilities

The previous entries describe the kernel as an abstract entity. The role a driver plays inside that kernel is concrete and limited. A driver is the component that makes a specific piece of hardware, or a specific pseudo-device, usable through the kernel's existing subsystems. A storage driver exposes a disk through GEOM and the buffer cache so that filesystems can use it. A network driver exposes a NIC through `ifnet` so that the network stack can use it. A character driver exposes a device node in `/dev` so that userland programs can use it through ordinary file system calls. Drivers do not invent interfaces; they implement the ones the kernel already publishes.

That framing is worth keeping close while you read a new driver. Finding the two ends, the hardware end (registers, interrupts, DMA) and the subsystem end (the `ifnet`, the `cdev`, the GEOM provider), is the single best way to orient yourself in an unfamiliar driver. Section 2 puts more shape on the subsystem end.

## Section 2: What Is a Device Driver?

A device driver is the piece of kernel code that makes a particular device, real or virtual, usable through the operating system's regular interfaces. It lives on two fronts at once. On one side it speaks the hardware's language: register accesses, interrupts, DMA descriptors, protocol sequences. On the other side it speaks one of FreeBSD's standard subsystem interfaces: `cdevsw` for character devices, `ifnet` for network interfaces, GEOM for storage. A driver's job is to translate between those two sides, correctly and safely, for the lifetime of the device.

This section names the driver types the book cares about, the role the driver plays in the hardware-to-userland path, and the mechanics by which FreeBSD loads a driver and binds it to a device.

### Driver Types: Character, Block, Network, Pseudo

**What it is.** A classification by the subsystem interface the driver publishes. The vocabulary is older than FreeBSD and the historical category names still appear, but on modern FreeBSD the exact boundaries differ from the textbook picture.

- A **character device driver** exposes the hardware as a stream through a `cdev` node in `/dev`. It registers a `struct cdevsw` with entry points such as `d_open`, `d_close`, `d_read`, `d_write`, and `d_ioctl`, and the kernel routes corresponding system calls to those entry points. Pseudo-devices (drivers with no real hardware behind them, like `/dev/null` or `/dev/random`) are character devices too.
- A **block device driver** in the historical UNIX sense exposed a fixed-size disk through a block-oriented cache. In modern FreeBSD, disk-like devices are presented through GEOM, not through a separate block-`cdevsw`; the entries in `/dev` for a disk (`/dev/ada0`, `/dev/da0`) are still `cdev` nodes, but their substance is a GEOM provider, and filesystems talk to them through the BIO layer rather than through raw read/write.
- A **network driver** exposes a network interface through `ifnet`. It does not use `cdevsw` at all; it registers with the network stack, receives packets as `struct mbuf` chains, sends packets through the stack's transmit queues, and participates in link-state events.
- A **pseudo-device driver** is any driver with no real hardware behind it. It may be a character device (`/dev/null`, `/dev/random`), a network device (`tun`, `tap`, `lo`), or a storage provider (`md`, `gmirror`). The word "pseudo" refers only to the absence of hardware; the subsystem integration is the same as for a real device.

**Why this matters to driver writers.** The type of driver you are writing determines the interface you must implement. A character-device driver spends its time in `d_open`, `d_close`, `d_read`, `d_write`, `d_ioctl`. A network driver spends its time in `if_transmit`, `if_input`, and interrupt-driven receive paths. A storage driver spends its time in GEOM start routines and BIO completion. Picking the right type early saves a rewrite; picking the wrong one is an expensive mistake.

**How this shows up in FreeBSD.** Three separate sets of headers and chapters of the tree handle the three main types:

- Character drivers register through `/usr/src/sys/sys/conf.h` (`struct cdevsw`, `make_dev_s`) and are implemented all across `/usr/src/sys/dev/`.
- Network drivers register through `/usr/src/sys/net/if.h` (`struct ifnet`) and have their stack integration in `/usr/src/sys/net/` and `/usr/src/sys/netinet/`.
- Storage drivers register through the GEOM framework under `/usr/src/sys/geom/` and use `struct bio` for I/O requests.

**Common trap.** Reaching for `cdevsw` to expose a disk. The "everything is a file" intuition is half right, but a modern FreeBSD disk is a GEOM provider; a raw `cdevsw` interface skips the cache, the partitioning layer, and the transforms the rest of the system expects.

**Where the book teaches this.** Chapter 6 names the categories and positions them inside Newbus. Chapter 7 writes the first character driver from scratch. Chapter 27 covers storage and GEOM. Chapter 28 covers network drivers.

**What to read next.** `cdevsw(9)`, `ifnet(9)`, and a compact pseudo-device such as `/usr/src/sys/dev/null/null.c`. Read the `null` driver in one sitting; it is the cleanest example of a complete character driver in the tree.

### The Role of a Driver in Hardware Communication

**What it is.** The driver is the only kernel code that talks to its hardware. Nothing else in the kernel accesses the device's registers, arms its interrupts, or queues its DMA transfers. All other subsystems reach the device through the driver.

**Why this matters to driver writers.** This exclusivity is why the driver must be trustworthy. The network stack cannot double-check whether a packet was really transmitted; it trusts the network driver. The filesystem cannot double-check whether a block really reached the disk; it trusts the storage driver. When the driver lies (by completing a transfer before the hardware is finished, for example), the rest of the system believes the lie and carries on. Most catastrophic failures in real systems come from this class of subtle driver dishonesty, not from outright crashes.

**How this shows up in FreeBSD.** In the shape of the driver's public functions. A character driver's `d_read` returns only after the data is really in the user buffer or an error has occurred. A network driver's `if_transmit` tells the stack whether the packet is owned by the driver now or still by the stack. A storage driver's `g_start` takes responsibility for completing the `bio` through `biodone(9)` eventually. Every one of those contracts is a promise the driver is making on behalf of the hardware.

**Common trap.** Returning early from a completion path because "the hardware usually works". On a good day, nothing fails. On a bad day, the stack thinks a packet was sent that was not, a filesystem thinks a block was written that was not, and the resulting corruption is impossible to track back to its source.

**Where the book teaches this.** Chapter 6 sets up the contract. Chapter 19 makes it rigorous for interrupt-driven completion. Chapter 21 does the same for DMA.

**What to read next.** `biodone(9)`, and the transmit/receive path of a small NIC driver.

### How Drivers Are Loaded and Bound to Devices

**What it is.** FreeBSD drivers reach a running kernel in one of two ways. Either they are compiled into the kernel binary (built-in drivers, activated during boot) or they are built as kernel-loadable modules (`.ko` files) that the administrator loads at run time with `kldload(8)`. Once a driver is present in the kernel, Newbus offers it the devices it claims.

The binding dance is the *probe/attach* cycle. Every bus enumerates its children at boot or on hot-plug and asks each registered driver whether it can handle each child. The driver's `probe` function examines the child (vendor/device ID for PCI, VID/PID for USB, FDT compatibility string for embedded systems, a class register for generic buses) and returns a priority value saying how confident it is. The bus picks the highest-priority match and calls that driver's `attach` function. From that point on, the device belongs to the driver, and the driver's softc holds the device's state.

**Why this matters to driver writers.** Every FreeBSD driver ends with a short block of boilerplate that registers it for this dance. The boilerplate is the contract between the driver and the rest of the kernel. Get it wrong and the driver never attaches; get it right and the kernel will attach, detach, suspend, resume, and unload your driver at the moments it chooses.

**How this shows up in FreeBSD.** Five pieces, all compact:

- A `device_method_t` array that lists the driver's implementations of the standard device methods, ending with `DEVMETHOD_END`.
- A `driver_t` structure that binds a driver name, the method table, and a softc size.
- A `DRIVER_MODULE(name, busname, driver, evh, arg)` invocation at the bottom of the file, which registers the driver with the named bus. The `evh` and `arg` fields pass an optional event handler; most drivers leave them as `0, 0`.
- A `MODULE_VERSION(name, version)` invocation to advertise the module's ABI version.
- Optional `MODULE_DEPEND(name, other, min, pref, max)` lines if the driver depends on another module.

These live in `/usr/src/sys/sys/module.h` and `/usr/src/sys/sys/bus.h`. The `DEVMETHOD` macro hooks the driver's `probe`, `attach`, `detach`, and optional `suspend`/`resume`/`shutdown` functions into the kobj method dispatcher that Newbus uses internally.

**Common trap.** Forgetting `MODULE_VERSION`. The module may load and work on your machine, and then fail to load on someone else's because the kernel has no way to check ABI compatibility. The version declaration is not ceremonial; it is how the module loader keeps pieces compatible.

**Where the book teaches this.** Chapter 6 introduces the Newbus tree. Chapter 7 writes the full boilerplate end to end and uses `kldload(8)` to exercise it.

**What to read next.** `DRIVER_MODULE(9)`, `MODULE_VERSION(9)`, `kld(9)`, `kldload(8)`, and the compact `/usr/src/sys/dev/null/null.c` as a complete example.

### Connecting Hardware, Kernel Subsystems, and Userland

**What it is.** A short way of stating what a driver does viewed from the top. Hardware is on one side. Userland is on the other. In between are the kernel subsystems (VFS, GEOM, `ifnet`, `cdev`). The driver is the component that lets bytes flow through that sandwich.

A rough picture, in one direction:

```text
userland                kernel subsystems        driver             hardware
--------                ------------------       ------             --------
cat(1)                  -> open(2) / read(2)
                        -> cdev switch                              ADC, NIC,
                           (d_read)            -> driver entry      disk, etc.
                                                   point
                                                -> register read,
                                                   MMIO, DMA
                                                                    -> bytes

                                                <- sync, interrupt,
                                                   completion
                        <- mbuf chain / bio /
                           uiomove back
                        <- read(2) returns
```

The picture is not literal code. It is the shape of responsibility. Every driver fits some variant of it.

**Why this matters to driver writers.** When you are unsure where a piece of code belongs, ask which layer of the sandwich is responsible for the current concern. A user-pointer validation belongs at the subsystem boundary. A register access belongs in the driver. A per-device state update belongs in the softc. Once the layer is clear, the names and primitives follow.

**Where the book teaches this.** Chapter 6 draws the picture for character drivers. Chapter 27 redraws it for GEOM. Chapter 28 redraws it for `ifnet`. The shape is always recognisable.

**What to read next.** The top of `/usr/src/sys/dev/null/null.c` for a minimal character variant, and the top of `/usr/src/sys/net/if_tuntap.c` for a minimal network variant.

## Section 3: Kernel Data Structures You Will See Often

Certain shapes appear in nearly every FreeBSD driver. A linked list of pending requests. A buffer that crosses the kernel/user boundary. A lock around the softc. A condition variable waited on from one thread and signalled from another. None of these is complicated in isolation, but meeting them for the first time inside real code is where the mental model often breaks down. This section names the families, explains what role they play, and points you back to the chapter that teaches them in full. Appendix A has the detailed APIs; Appendix B has the algorithmic patterns. This section is the one-page orientation.

### Lists and Queues from `<sys/queue.h>`

**What it is.** A header-only family of intrusive-list macros. You embed a `TAILQ_ENTRY`, `LIST_ENTRY`, or `STAILQ_ENTRY` inside your element structure, define a head, and the macros give you insertion, removal, and traversal without any heap allocation for list nodes. Four flavours exist: `SLIST` (singly linked), `LIST` (doubly linked, insertion at head), `STAILQ` (singly linked with fast tail insertion), and `TAILQ` (doubly linked with tail insertion and O(1) arbitrary removal).

**Why this matters to driver writers.** Almost every per-driver collection in the tree is one of these. Pending commands, open file handles, registered callbacks, queued `bio` requests. Picking the right flavour for the access pattern produces code that is short, predictable, and lockable with a single mutex. Picking the wrong one produces needless pointer bookkeeping or surprise O(n) removal.

**How this shows up in FreeBSD.** The macros live in `/usr/src/sys/sys/queue.h`. A softc often holds its queue heads as ordinary fields. Every traversal is wrapped by the driver's lock. The safe-against-removal variants (`TAILQ_FOREACH_SAFE`, `LIST_FOREACH_SAFE`) are the ones you want any time the body of the loop might unlink the current element.

**Common trap.** Using a plain `LIST_FOREACH` while freeing elements inside the loop body. The iterator dereferences the element's link pointer after you have already freed it. Switch to the `_SAFE` variant or rearrange the loop.

**Where the book teaches this.** Chapter 5 introduces the macros in kernel C. Chapter 11 uses them with locking. Appendix B has the full pattern-oriented treatment.

**What to read next.** `queue(3)`, `/usr/src/sys/sys/queue.h`, and any driver softc that holds an in-flight request list.

### Kernel Buffers: `mbuf`, `buf`, and `bio`

**What it is.** Three buffer representations that appear in specific subsystems. `struct mbuf` is the networking packet-representation type, a linked list of small fixed-size units that together hold a packet and its metadata. `struct buf` is the storage buffer-cache unit, used by the VFS and the legacy block layer. `struct bio` is the GEOM I/O-request structure, the modern unit of storage I/O that a storage driver completes with `biodone(9)`.

**Why this matters to driver writers.** You will not meet all three at once. A network driver thinks in `mbuf` chains. A storage driver thinks in `bio` requests and, at the VFS layer, in `buf` entries. A character driver rarely sees any of them; it uses `struct uio` instead. Knowing which buffer type your subsystem uses tells you which completion convention applies and which headers to include. Mixing them up is a signal that the driver type is wrong or that a layer boundary is being violated.

**How this shows up in FreeBSD.** `struct mbuf` is defined in `/usr/src/sys/sys/mbuf.h`. `struct buf` is defined in `/usr/src/sys/sys/buf.h`. `struct bio` is defined in `/usr/src/sys/sys/bio.h`. Each has its own allocation, reference-counting, and completion rituals; the driver never invents a substitute.

**Common trap.** Allocating a plain byte buffer for network transmit because it looks easier. The network stack hands you an `mbuf`; the network stack expects an `mbuf` back. Converting in and out of the right type is almost always a mistake, and it breaks zero-copy fast paths that the stack relies on.

**Where the book teaches this.** Chapter 27 introduces `bio` in the storage context. Chapter 28 introduces `mbuf` in the network context. Appendix E will have deeper entries for each.

**What to read next.** `mbuf(9)`, `bio(9)`, `buf(9)`.

### Locking: Mutex, Spin Mutex, Sleep Lock, Condition Variable

**What it is.** A family of kernel primitives for ordering access to shared data. Four kinds matter at the appendix level:

- A **default mutex** (`mtx(9)` with `MTX_DEF`) is the everyday sleep-capable mutex. A thread that cannot immediately acquire it goes to sleep until the owner releases. It is the right default for almost every softc field.
- A **spin mutex** (`mtx(9)` with `MTX_SPIN`) is an interrupt-safe busy-wait lock. A thread that cannot immediately acquire it spins. Spin mutexes are used in the tiny set of contexts where sleeping is forbidden (interrupt filters and a few scheduler paths). They are not a performance optimisation; they are a correctness tool for a narrow case.
- A **sleepable shared/exclusive lock** (`sx(9)`) is the right choice for long read-mostly critical sections that may sleep while holding the lock. It is used by code that performs operations the kernel might block on (for example, allocating memory with `M_WAITOK` while holding the lock).
- A **condition variable** (`cv(9)`) is a synchronisation primitive for waiting on a predicate. A thread holds a mutex, checks a condition, and if the condition is false, calls `cv_wait` which atomically drops the mutex, sleeps, and reacquires the mutex on wakeup. Another thread changes the predicate under the same mutex and calls `cv_signal` or `cv_broadcast`. A condition variable never exists without an accompanying mutex.

**Why this matters to driver writers.** Every non-trivial driver keeps shared state: the softc itself, an in-flight request list, a pending I/O counter, a reference count. Protecting that state correctly is the difference between a driver that works under load and a driver that fails intermittently in ways nobody can reproduce. The FreeBSD locking primitives are the tools you reach for every time.

**How this shows up in FreeBSD.** The headers are `/usr/src/sys/sys/mutex.h` for the regular and spin mutexes, `/usr/src/sys/sys/sx.h` for the shared/exclusive lock, and `/usr/src/sys/sys/condvar.h` for condition variables. The convention is to store the lock inside the softc and name it after the softc field: a driver that protects its work queue usually has something like `mtx_init(&sc->sc_lock, device_get_nameunit(dev), NULL, MTX_DEF)` in `attach` and a matching `mtx_destroy` in `detach`.

**Common trap.** Two, both common. The first is taking a sleep mutex from an interrupt filter. A filter runs in a context that cannot sleep; a sleep mutex may block; the kernel detects this with `witness(9)` in a development build. The second is reading the state the condition variable guards *outside* the mutex. The mutex-predicate-wait pattern relies on the predicate and the wait being atomic with respect to the signaller; breaking that atomicity reintroduces the race the condition variable was supposed to eliminate.

**Where the book teaches this.** Chapter 11 introduces mutexes and spin mutexes. Chapter 12 adds condition variables and shared/exclusive locks. Appendix B collects the reasoning patterns.

**What to read next.** `mtx(9)`, `sx(9)`, `cv(9)`, `witness(9)`, and the top of a real driver's softc structure to see the lock and its users in one place.

### `softc`: The Per-Device Driver Structure

**What it is.** The per-device state structure that the driver defines and the kernel allocates on its behalf. "Softc" is short for "software context". It holds everything the driver needs to know about one specific instance of the device it drives: the `device_t` back-pointer, the lock, the `bus_space` tag and handle for MMIO, the allocated resources, any in-flight state, counters, and sysctl nodes. When FreeBSD attaches a driver to a device, it allocates a softc of the size the driver declared in its `driver_t` and gives the driver a pointer to it through `device_get_softc(9)`.

**Why this matters to driver writers.** Every function in the driver reaches the softc the same way: `struct mydev_softc *sc = device_get_softc(dev)`. Everything specific to this particular device lives behind that pointer. A driver that attaches to three devices gets three independent softcs, each with its own lock, its own state, its own hardware view. Keeping the softc the single owner of per-device state is what makes the code reentrant across multiple devices.

**How this shows up in FreeBSD.** The pattern is uniform across the tree. In the driver:

- A `struct mydev_softc` definition at the top of the file.
- `.size = sizeof(struct mydev_softc)` in the `driver_t` initialiser.
- `struct mydev_softc *sc = device_get_softc(dev)` at the top of every function that receives a `device_t`.
- A `sc->sc_lock` field initialised in `attach` and destroyed in `detach`.

The kernel zeroes the softc before handing it to the driver, so a fresh attach always starts in a known state.

**Common trap.** Stashing state in file-scope globals because "there is only one device". Then someone attaches two, or builds a VM with two, and the second device silently corrupts the first. Every driver, no matter how simple it looks, keeps state in the softc.

**Where the book teaches this.** Chapter 6 introduces the concept. Chapter 7 writes a full softc and uses it end to end. Every chapter from Chapter 8 onward assumes the pattern.

**What to read next.** `device_get_softc(9)`, the `softc` block at the top of `/usr/src/sys/dev/null/null.c`, and the softc of any small PCI driver.

### Why These Structures Keep Reappearing

The families above (queues, buffers, locks, and the softc) recur because they correspond to the four questions every driver eventually has to answer. What pending work do I hold? What bytes am I moving? How do I serialise access to my own state? Where do I keep the state of one instance of my device? Once you recognise that framing, driver code starts to read very differently. The unfamiliar parts are the details of a specific device or bus; the familiar parts are the four answers, in four standard shapes, used by every driver in the tree.

## Section 4: Kernel Compilation and Booting

A driver runs inside a kernel. That kernel did not appear from nothing; something had to compile it, something had to load it into memory, something had to call its entry point, and something had to set up the environment in which your driver's `attach` function eventually runs. This section traces that chain at concept level, so that when a bug happens early in the boot, you have a mental picture of what the system was trying to do.

### The Boot Process Overview

**What it is.** The sequence of events from power-on to a running FreeBSD kernel with userland processes. On a typical x86 machine it is roughly: firmware (BIOS or UEFI), first-stage boot (`boot0` on BIOS, the UEFI boot manager on UEFI), the FreeBSD loader (`loader(8)`), the architecture-specific kernel entry (`hammer_time` on amd64, called from assembly), the machine-independent startup (`mi_startup`), the `SYSINIT` chain, and finally the first userland process (`/sbin/init`). On ARM64 and other architectures, the firmware and machine-dependent parts differ; the machine-independent kernel startup is the same.

**Why this matters to driver writers.** Two reasons. First, many driver failures happen during `SYSINIT` processing, before there is a console to print to, and you need to know where you are when that happens. Second, the order in which subsystems initialise determines when your driver's bus has enumerators, when memory allocators are available, when the root filesystem is mounted, and when it is safe to talk to userland. A driver that tries to allocate memory too early will fail; a driver that tries to open a file before the root filesystem is mounted will also fail. The `SYSINIT` mechanism names the stages explicitly so that ordering is visible in the code.

**How this shows up in FreeBSD.** Each stage has a file you can open. The boot loader lives under `/usr/src/stand/`; the amd64 early C entry is `hammer_time` in `/usr/src/sys/amd64/amd64/machdep.c` (called from `/usr/src/sys/amd64/amd64/locore.S`); the machine-independent startup is `mi_startup` in `/usr/src/sys/kern/init_main.c`; and `start_init` in the same file is what the init process runs when it is finally scheduled, execing `/sbin/init` as PID 1.

**Common trap.** Treating boot as opaque. The `SYSINIT` mechanism makes the stages explicit and inspectable, and a driver that understands its place in them is much easier to debug.

**Where the book teaches this.** Chapter 2 walks through boot in lab context. Chapter 6 connects the driver-loading sequence to the kernel's startup picture.

**What to read next.** `boot(8)`, `loader(8)`, `/usr/src/sys/kern/init_main.c`, and the `SYSINIT` section of `/usr/src/sys/sys/kernel.h`.

### From Power-On to `init`: A Compact Timeline

A thumbnail picture of the whole sequence. The names on the right are the functions or manual pages you would open to look closer.

```text
+-----------------------------------------------+-----------------------------+
| Stage                                         | Where to look               |
+-----------------------------------------------+-----------------------------+
| Firmware: BIOS / UEFI starts executing        | (hardware / firmware)       |
+-----------------------------------------------+-----------------------------+
| First-stage boot: boot0 / UEFI boot manager   | /usr/src/stand/              |
+-----------------------------------------------+-----------------------------+
| FreeBSD loader: selects kernel and modules    | loader(8), loader.conf(5)   |
+-----------------------------------------------+-----------------------------+
| Kernel entry (amd64): hammer_time() + locore  | sys/amd64/amd64/machdep.c,  |
|                                               | sys/amd64/amd64/locore.S    |
+-----------------------------------------------+-----------------------------+
| MI startup: mi_startup() drives SYSINITs      | sys/kern/init_main.c        |
+-----------------------------------------------+-----------------------------+
| SYSINIT chain: subsystems init in order       | sys/sys/kernel.h            |
|   SI_SUB_VM, SI_SUB_LOCK, SI_SUB_KLD, ...     |                             |
|   SI_SUB_DRIVERS, SI_SUB_CONFIGURE, ...       |                             |
+-----------------------------------------------+-----------------------------+
| Bus probe and attach: drivers bind to devices | sys/kern/subr_bus.c         |
+-----------------------------------------------+-----------------------------+
| SI_SUB_CREATE_INIT / SI_SUB_KTHREAD_INIT:     | sys/kern/init_main.c        |
|   init proc forked, then made runnable        |                             |
+-----------------------------------------------+-----------------------------+
| start_init() mounts root, execs /sbin/init    | sys/kern/init_main.c,       |
|   (PID 1)                                     | sys/kern/vfs_mountroot.c    |
+-----------------------------------------------+-----------------------------+
| Userland startup: rc(8), daemons, logins      | rc(8), init(8)              |
+-----------------------------------------------+-----------------------------+
```

The table is a map, not a contract. Many details differ per architecture, and the boundaries between stages shift as the kernel evolves. What does not change is the direction: small, simple environment at the top, increasingly functional environment at the bottom, userland at the end.

### Boot Loaders and Kernel Modules at Boot Time

**What it is.** The FreeBSD loader (`/boot/loader`) is a small program that the first-stage boot code hands control to. Its job is to read `/boot/loader.conf`, load the kernel (`/boot/kernel/kernel`), load any modules requested by configuration, hand tunables and hints to the kernel, and transfer control to the kernel's entry point. It also provides the boot menu, the ability to boot a different kernel for recovery, and the facility to load a module before the kernel starts.

**Why this matters to driver writers.** Some drivers must be present before the root filesystem is mounted (storage controllers, for example). Those drivers are either compiled into the kernel or loaded by the loader from `/boot/loader.conf`. A driver that is loaded after root mount (through `kldload(8)`) cannot drive the disk that holds `/`. Knowing the difference is part of planning when a driver should attach.

**How this shows up in FreeBSD.** `/boot/loader.conf` is the text file that lists modules to load at boot. `loader(8)` and `loader.conf(5)` describe the mechanism. The loader also provides facilities such as `kenv(2)` tunables that the kernel can read in early `SYSINIT` stages.

**Common trap.** Relying on boot-time loader variables from kernel code that runs after root mount. Those variables are available; they are just not the right place for runtime configuration. Use `sysctl(9)` tunables for runtime, `kenv` only for early init decisions.

**Where the book teaches this.** Chapter 2 uses the loader while setting up the lab. Chapter 6 introduces `kldload(8)` for run-time driver loading.

**What to read next.** `loader(8)`, `loader.conf(5)`, `kenv(2)`.

### Kernel Modules and Driver Attachment

**What it is.** A kernel module is a compiled `.ko` file that the kernel can load at runtime (or that the loader can load before kernel entry). Modules are how most drivers reach the kernel during normal operation. The module framework handles symbol resolution, versioning (`MODULE_VERSION`), dependencies (`MODULE_DEPEND`), and the registration of any `DRIVER_MODULE` declarations the file contains.

**Why this matters to driver writers.** Most of your development cycle will look like this: edit source, run `make`, `kldload ./mydriver.ko`, test, `kldunload mydriver`, repeat. The module framework makes that cycle fast and safe, and it also makes the production deployment story clean (the same `.ko` that you test is what ships). The loader and the run-time `kldload(8)` use the same module format.

**How this shows up in FreeBSD.** Every driver file ends with `DRIVER_MODULE`, `MODULE_VERSION`, and optionally `MODULE_DEPEND`. The declarations register the driver's bus-attachment metadata and module-level metadata with the kernel. When the module is loaded, the kernel runs the module's event handler for `MOD_LOAD`, which (among other things) registers the driver with Newbus. Newbus then offers the driver each device of the right bus, calling `probe` on each.

**Common trap.** Forgetting to call `bus_generic_detach`, `bus_release_resource`, or `mtx_destroy` from `detach`. The module unload succeeds, the driver is gone, but the resources leak. Subsequent reloads accumulate leaks until something obvious breaks.

**Where the book teaches this.** Chapter 6 introduces the module/driver relationship. Chapter 7 exercises the full edit-build-load-test-unload cycle.

**What to read next.** `kld(9)`, `kldload(8)`, `kldstat(8)`, `DRIVER_MODULE(9)`, `MODULE_VERSION(9)`.

### `init_main` and the `SYSINIT` Chain

**What it is.** `mi_startup` is the machine-independent entry point that the architecture-specific boot code calls after early assembly (on amd64, `hammer_time` returns to `locore.S`, which calls `mi_startup`). Its body is almost entirely a loop that walks a sorted list of `SYSINIT` records and calls each one. Each record is registered at compile time by a `SYSINIT(...)` macro and carries a subsystem identifier (`SI_SUB_*`) and an order within the subsystem (`SI_ORDER_*`). The linker collects them all into a single section; `mi_startup` sorts and runs them. The init process itself is created in a stopped state at `SI_SUB_CREATE_INIT`, made runnable at `SI_SUB_KTHREAD_INIT`, and once it is scheduled it runs `start_init`, which mounts the root filesystem and execs `/sbin/init` as PID 1.

**Why this matters to driver writers.** Most drivers never touch `SYSINIT` directly; `DRIVER_MODULE` wraps the right registration for them. But a few pieces of infrastructure (kernel threads that must exist before drivers attach, subsystem-wide initialisers, early event handlers) use `SYSINIT` directly, and when you read such code it helps to know what the declaration is registering. The `SI_SUB_*` names make the ordering auditable, and the numeric spacing between them leaves room for future stages.

**How this shows up in FreeBSD.** The `SYSINIT` macro and the `SI_SUB_*` constants are in `/usr/src/sys/sys/kernel.h`. The driver-relevant stages include `SI_SUB_VM` (virtual memory up), `SI_SUB_LOCK` (lock initialisation), `SI_SUB_KLD` (module system), `SI_SUB_DRIVERS` (early driver-subsystem init), `SI_SUB_CONFIGURE` (Newbus probe and attach), `SI_SUB_ROOT_CONF` (candidate root devices identified), `SI_SUB_CREATE_INIT` (init process forked), and `SI_SUB_KTHREAD_INIT` (init made runnable). `mi_startup` is in `/usr/src/sys/kern/init_main.c`, and `start_init` is in the same file.

**Common trap.** Assuming your driver's `attach` runs very early. It runs at `SI_SUB_CONFIGURE`, which is after locks, memory, and most subsystems are up. If you need to coordinate with something earlier, either use the event-handler system (`eventhandler(9)`) to hook a well-defined event, or place the work at the right `SYSINIT` stage.

**Where the book teaches this.** Chapter 2 introduces the picture at boot-time. Chapter 6 connects `DRIVER_MODULE` to the `SYSINIT` chain conceptually. Appendix E goes into the subsystem-stage detail for readers who need it.

**What to read next.** The `SYSINIT` definitions in `/usr/src/sys/sys/kernel.h`, the loop in `/usr/src/sys/kern/init_main.c`, and `/usr/src/sys/kern/subr_bus.c` for the Newbus side.

### Kernel Compilation in One Paragraph

A FreeBSD kernel is built from source by the ordinary `make` system invoked from `/usr/src`. The build takes a kernel configuration file (typically under `/usr/src/sys/amd64/conf/GENERIC` or a customised file you keep alongside it), compiles the selected options and device entries, and links the result into `kernel` plus a set of `.ko` files for devices not statically included. The standard commands are `make buildkernel KERNCONF=MYCONF` and `make installkernel KERNCONF=MYCONF`, run from `/usr/src`. For out-of-tree modules, the build uses `bsd.kmod.mk`, a short Makefile fragment that knows how to compile one or more `.c` files into a `.ko` file with the right kernel flags. Chapter 2 walks through both paths in lab context; the details are not repeated here.

## Quick-Reference Tables

The compact tables below are meant for scanning. They do not replace the sections above; they help you point at the right section fast.

### Kernel Space vs User Space at a Glance

| Property                    | Kernel space                           | User space                              |
| :-------------------------- | :------------------------------------- | :-------------------------------------- |
| Privilege                   | Full CPU privilege                     | Reduced, CPU-enforced                   |
| Address space               | One, shared by all kernel threads      | One per process, isolated               |
| Memory stability            | Stable while allocated                 | May be paged, unmapped, moved           |
| Direct hardware access      | Yes                                    | No                                      |
| Failure blast radius        | Whole machine                          | One process                             |
| How to cross over           | System call trap                       | `syscall` instruction (architecture)    |
| Data copy primitive (into)  | `copyin(9)`, `uiomove(9)`              | (no equivalent; kernel does the copy)   |
| Data copy primitive (out)   | `copyout(9)`, `uiomove(9)`             | (no equivalent; kernel does the copy)   |

### Driver Type Comparison

| Driver type        | Registers with       | Main entry points               | Main buffer type | Typical devices            |
| :----------------- | :------------------- | :------------------------------ | :--------------- | :------------------------- |
| Character          | `cdevsw` in devfs    | `d_open`, `d_read`, `d_ioctl`   | `struct uio`     | Serial, sensors, `/dev/*`  |
| Network            | `ifnet`              | `if_transmit`, receive path     | `struct mbuf`    | NICs, `tun`, `tap`         |
| Storage (GEOM)     | GEOM class/provider  | `g_start`, `biodone(9)`         | `struct bio`     | Disks, `md`, `geli`        |
| Pseudo (character) | `cdevsw` in devfs    | Same as character               | `struct uio`     | `/dev/null`, `/dev/random` |
| Pseudo (network)   | `ifnet`              | Same as network                 | `struct mbuf`    | `tun`, `tap`, `lo`         |

### Locking Primitive Selection

| You need...                                 | Use                                    |
| :------------------------------------------ | :------------------------------------- |
| A default lock around softc fields          | `mtx(9)` with `MTX_DEF`                |
| A lock safe to hold in an interrupt filter  | `mtx(9)` with `MTX_SPIN`               |
| Many readers, occasional writer, may sleep  | `sx(9)`                                |
| Wait for a predicate under a mutex          | `cv(9)` together with the mutex        |
| Atomic single-word read-modify-write        | `atomic(9)`                            |

### `softc` Lifecycle Checkpoint

| Phase     | Typical softc actions                                          |
| :-------- | :------------------------------------------------------------- |
| `probe`   | Read IDs, return `BUS_PROBE_*`. No softc allocation yet.        |
| `attach`  | `device_get_softc`, init lock, allocate resources, setup state. |
| Runtime   | All operations dereference `sc`, all state updates under lock.  |
| `suspend` | Save volatile hardware state, stop pending work.                |
| `resume`  | Restore state, restart work.                                    |
| `detach`  | Stop work, free resources in reverse order, destroy lock.       |

### Boot-to-Init Quick Map

| Phase                                       | Driver-relevant fact                                             |
| :------------------------------------------ | :--------------------------------------------------------------- |
| Firmware + loader                           | Kernel and preloaded modules reach memory.                        |
| Kernel entry (`hammer_time` on amd64)       | Machine-dependent early setup; no drivers yet.                    |
| `mi_startup` / `SYSINIT` chain              | Subsystems initialise in `SI_SUB_*` order.                        |
| `SI_SUB_DRIVERS` / `SI_SUB_CONFIGURE`       | Buses enumerate; compiled-in and preloaded drivers `probe` / `attach`. |
| `SI_SUB_ROOT_CONF`                          | Candidate root devices identified.                                |
| `SI_SUB_CREATE_INIT` / `SI_SUB_KTHREAD_INIT`| Init process forked, then made runnable.                          |
| `start_init` runs                           | Root filesystem mounted; `/sbin/init` execed as PID 1.            |
| Runtime `kldload(8)`                        | Additional drivers attach after this point.                       |

## Wrapping Up: How These OS Concepts Support Driver Development

Each of the concepts in this appendix is already in use somewhere in the book. The appendix only assembles the pieces so that the reader can glance at the whole picture when one part feels out of focus.

The kernel/user boundary is the reason driver code looks different from ordinary C. Every unusual rule, from `copyin(9)` to the short spin-safe interrupt filter, exists because the kernel runs in a privileged world that cannot afford to trust the less privileged one. Keep that framing and the rest follows.

The driver types name the contracts with the rest of the kernel. Character, network, and GEOM are not ornaments; they are the standard interfaces that let userland and other subsystems reach your device without learning anything about its hardware. When you begin a new driver, naming the type is the first architectural decision you make.

The recurring data structures name the shapes inside the driver. A queue for pending work. A buffer for bytes moving through the subsystem. A mutex around shared state. A softc that owns everything about one device. The shapes are small; the discipline they encode is what keeps the driver correct.

The boot sequence names the stage on which your driver performs. The kernel did not spring into existence; it was assembled by a chain of well-defined steps, and your driver enters during one specific step at `SI_SUB_CONFIGURE`. Understanding the order of the steps turns early-boot bugs from mysteries into ordinary kernel diagnostics.

Three habits keep these concepts active instead of dormant.

The first is asking which side of the boundary you are on. Before you touch a pointer, ask whether it is a user pointer, a kernel pointer, or a bus address; the correct primitive follows immediately.

The second is asking which subsystem you are speaking to. Before you reach for a buffer type, ask whether the code is in the character, network, or GEOM path; the right buffer follows immediately.

The third is keeping a short companion sheet for the concepts you use most. The files under `examples/appendices/appendix-d-operating-system-concepts/` are meant for exactly that. A kernel-vs-user-space cheatsheet, a driver-type comparison, a softc lifecycle checklist, a boot-to-init timeline, and a "where drivers fit in the OS" diagram. The teaching is in this appendix; the application is in those sheets.

With that, the operating-system side of the book has a consolidated home. The chapters keep teaching; this appendix keeps naming; the examples keep reminding. When a reader closes this appendix and opens an unfamiliar driver, the mental model is ready, and every line of kernel code has a place to attach to.
