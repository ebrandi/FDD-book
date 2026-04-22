---
title: "Navigating FreeBSD Kernel Internals"
description: "A navigation-oriented map of the FreeBSD kernel subsystems that surround driver work, with the structures, source-tree locations, and driver touchpoints that help a reader orient themselves quickly."
appendix: "E"
lastUpdated: "2026-04-20"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "TBD"
estimatedReadTime: 40
---

# Appendix E: Navigating FreeBSD Kernel Internals

## How to Use This Appendix

The main chapters teach you to build a FreeBSD device driver from the first `printf("hello")` module to a working PCI driver with DMA and interrupts. Underneath that progression is a large kernel with many moving parts, and the book cannot teach each of those parts from scratch without losing the thread of what you are actually trying to do. Most of the time you do not need to know every corner of the kernel. You just need to know where you are, which subsystem your current line of code is touching, which structure gives you the answer when you stop to look, and where in `/usr/src` the evidence lives.

This appendix is that map. It does not try to teach each subsystem from first principles. It takes the seven subsystems a driver author meets most often and, for each one, gives you the short version: what it is for, which structures matter, which APIs your driver is likely to cross, where to open a file and look, and what to read next. You can think of it as the field guide you keep next to the chapters, not a replacement for them.

### What You Will Find Here

Each subsystem is covered with the same small pattern, so that you can skim one and know where to look in the next.

- **What the subsystem is for.** A one-paragraph statement of the subsystem's responsibility.
- **Why a driver author should care.** The concrete reason your code meets this subsystem.
- **Key structures, interfaces, or concepts.** The short list of names that actually matter.
- **Typical driver touchpoints.** The specific places where a driver calls in, registers with, or receives a callback from the subsystem.
- **Where to look in `/usr/src`.** The two or three files that are worth opening first.
- **Manual pages and files to read next.** The next step when you want more depth.
- **Common beginner confusion.** The misunderstanding that costs people time.
- **Where the book teaches this.** Back-references into the chapters that use the subsystem in context.

Not every entry needs every label, and no entry tries to be exhaustive. The aim is pattern recognition, not a full subsystem manual.

### What This Appendix Is Not

It is not an API reference. Appendix A is the API reference and goes deeper into flags, lifecycle phases, and caveats for each call. When the question is *what does this function do* or *which flag is right*, Appendix A is the place to look.

It is not a concept tutorial either. Appendix D covers the operating-system mental model (kernel vs userland, driver types, the boot-to-init path), Appendix C covers the hardware model (physical memory, MMIO, interrupts, DMA), and Appendix B covers the algorithmic patterns (`<sys/queue.h>`, ring buffers, state machines, unwind ladders). If the question you want answered is "what is a process", "what is a BAR", or "which list macro should I use", one of those appendices is the right destination.

It is also not a complete subsystem reference. A full tour of the VFS, the VM, or the network stack would be its own book. What you get here is the ten per cent of each subsystem a driver author actually meets, in the order in which a driver author meets it.

## Reader Guidance

Three ways to use this appendix, each calling for a different reading strategy.

If you are **reading the main chapters**, keep the appendix open in a second window. When Chapter 5 introduces the kernel's memory allocators, glance at the Memory Subsystem section here to see where those allocators sit relative to UMA and the VM system. When Chapter 6 walks through `device_t`, softc, and the probe/attach lifecycle, the Driver Infrastructure section shows where those types fit into the Newbus layer. When Chapter 24 discusses `SYSINIT`, `eventhandler(9)`, and taskqueues, the Boot and Module System and Kernel Services sections give you the surrounding context in one page each.

If you are **reading unfamiliar kernel code**, treat the appendix as a translator. When you see `struct mbuf` in a function signature, jump to the Network Subsystem section. When you see `struct bio`, jump to File and VFS. When you see `kobj_class_t` or `device_method_t`, jump to Driver Infrastructure. The goal during exploration is not to master the subsystem, just to name it.

If you are **designing a new driver**, scan the subsystems your driver will touch before you start. A character driver for a small peripheral will lean on Driver Infrastructure and File and VFS. A network driver will add the Network Subsystem. A storage driver will add the GEOM and buffer cache layers. An early-boot driver on an embedded board will add the Boot and Module System section. Knowing which subsystems you will touch helps you guess the right headers and the right chapters before you write a line of code.

A few conventions apply throughout:

- Source paths are shown in the book-facing form, `/usr/src/sys/...`, matching the layout on a standard FreeBSD system. You can open any of them on your lab machine.
- Manual pages are cited in the usual FreeBSD style. Kernel-facing pages live in section 9: `kthread(9)`, `malloc(9)`, `uma(9)`, `bus_space(9)`, `eventhandler(9)`. Userland interfaces live in section 2 or 3 and are mentioned where they matter.
- When an entry points at a reading example, the file is one a beginner can read in one sitting. Larger files exist that also use each pattern; those are mentioned only when they are the canonical reference.

With that in mind, we start with a one-page orientation to the whole kernel before drilling into subsystems one at a time.

## How This Appendix Differs From Appendix A

A driver author ends up consulting two very different kinds of reference while working. One kind answers the question *what is the exact name of the function or flag I need*. That is Appendix A. The other kind answers the question *which subsystem am I in, and where does this piece fit*. That is this appendix.

Concretely, the difference shows up like this. When you want to know the signature of `malloc(9)`, the meaning of `M_WAITOK` versus `M_NOWAIT`, and which manual page to open, that is Appendix A. When you want to know that `malloc(9)` is a thin convenience on top of UMA, which in turn builds on the `vm_page_t` layer, which in turn depends on the per-architecture `pmap(9)`, that is this appendix.

Both appendices cite real source paths and real manual pages. The split is deliberate. Keeping the API lookup separate from the subsystem map makes each one short enough to actually use. If an entry here starts to look like Appendix A, it has drifted from its role, and the right move is to read Appendix A instead.

## A Map of the Major Subsystems

Before you walk into any one subsystem, the shape of the whole kernel is worth naming. The FreeBSD kernel is large, but the pieces a driver author meets fit into a small set of families. The diagram below is the simplest honest picture.

```text
+-----------------------------------------------------------------+
|                            USER SPACE                           |
|     applications, daemons, shells, tools, the libraries         |
+-----------------------------------------------------------------+
                               |
                  system-call trap (the boundary)
                               |
+-----------------------------------------------------------------+
|                           KERNEL SPACE                          |
|                                                                 |
|   +-----------------------+   +-----------------------------+   |
|   |   VFS / devfs / GEOM  |   |        Network stack        |   |
|   |  struct vnode, buf,   |   |   struct mbuf, socket,      |   |
|   |  bio, vop_vector      |   |   ifnet, route, VNET        |   |
|   +-----------------------+   +-----------------------------+   |
|                 \                     /                         |
|                  \                   /                          |
|                 Driver infrastructure (Newbus)                  |
|           device_t, driver_t, devclass_t, softc, kobj           |
|           bus_alloc_resource, bus_space, bus_dma                |
|                               |                                 |
|      Process/thread subsystem  |  Memory / VM subsystem          |
|      struct proc, thread       |  vm_map, vm_object, vm_page    |
|      ULE scheduler, kthreads   |  pmap, UMA, pagers             |
|                               |                                 |
|         Boot and module system (SYSINIT, KLD, modules)          |
|         Kernel services (eventhandler, taskqueue, callout)      |
+-----------------------------------------------------------------+
                               |
                      hardware I/O boundary
                               |
+-----------------------------------------------------------------+
|                             HARDWARE                            |
|     MMIO registers, interrupt controllers, DMA-capable memory   |
+-----------------------------------------------------------------+
```

Each of the labelled boxes above has a section below. The driver-infrastructure box in the middle is where every driver starts. The two boxes at the top are the subsystem entry points that a driver publishes to the rest of the kernel (character or storage on the left, network on the right). The two boxes in the middle rows are the horizontal services every driver relies on. The bottom boxes are the plumbing that gets the kernel running in the first place.

Most drivers touch only three or four of these boxes in detail. The appendix is organised so that you can read only the ones your driver actually uses.

## Process and Thread Subsystem

### What the subsystem is for

The process and thread subsystem manages every unit of execution inside FreeBSD. It owns the data structures that describe a running program, the scheduler that decides which thread runs next on which CPU, the machinery that creates and destroys kernel threads, and the rules that govern how a thread may block, sleep, or be preempted. Every line of kernel code, including your driver, is being executed by some thread, and the discipline the subsystem enforces is a direct constraint on what your driver is allowed to do.

### Why a driver author should care

Three practical reasons. First, the context your code runs in (interrupt filter, ithread, taskqueue worker, syscall thread from userland, dedicated kernel thread you spawned) decides whether you may sleep, may allocate memory with `M_WAITOK`, or may hold a sleep lock. Second, any driver that needs background work (a polling loop, a recovery watchdog, a deferred command handler) will create a kernel thread or a kproc to host it. Third, any driver that looks at the caller's process credentials (for security checks in `d_ioctl`, for example) reaches into the process structure.

### Key structures, interfaces, or concepts

- **`struct proc`** is the per-process descriptor. It records the process id, credentials, file-descriptor table, signal state, address space, and the list of threads that belong to the process.
- **`struct thread`** is the per-thread descriptor. It records the thread id, priority, run state, saved register context, the pointer to its owning `struct proc`, and the locks it currently holds. A FreeBSD kernel thread is also described by a `struct thread`; it simply has no userland side.
- **The ULE scheduler** is FreeBSD's default multi-processor scheduler. It assigns threads to CPUs, implements priority classes (real-time, time-sharing, idle), and honours interactivity and affinity hints. From a driver author's perspective, the most important fact about ULE is that it runs whichever thread is next, whenever a lock is released, a sleep ends, or an interrupt finishes; you cannot assume control of the CPU across such events.
- **`kthread_add(9)`** creates a new kernel thread inside an existing kernel process. Use it when you want a lightweight worker that shares state with an existing kproc (for example, extra worker threads inside a driver-specific kproc).
- **`kproc_create(9)`** creates a new kernel process, which comes with its own `struct proc` and one initial thread. Use it when you want an independent, top-level worker that `ps -axH` will show as a distinct name (for example, `g_event`, `usb`, `bufdaemon`).

### Typical driver touchpoints

- Interrupt handlers and `bus_setup_intr(9)` callbacks run in kernel-thread context created by the interrupt framework.
- A driver that needs long-running background work calls `kproc_create(9)` or `kthread_add(9)` from its `attach` path and joins the thread from `detach`.
- A driver that takes action on behalf of a user process reads `curthread` or `td->td_proc` to look at credentials, process id, or root-directory for validation.
- A driver that sleeps on a condition uses the sleep primitives, which record the sleeping thread and yield to the scheduler until woken.

### Where to look in `/usr/src`

- `/usr/src/sys/sys/proc.h` defines `struct proc` and `struct thread` and the macros that navigate between them.
- `/usr/src/sys/sys/kthread.h` declares the kernel-thread creation API.
- `/usr/src/sys/kern/kern_kthread.c` contains the implementation.
- `/usr/src/sys/kern/sched_ule.c` is the ULE scheduler source.

### Manual pages and files to read next

`kthread(9)`, `kproc(9)`, `curthread(9)`, `proc(9)`, and the header `/usr/src/sys/sys/proc.h`. If you want to see a driver that owns a kernel thread, `/usr/src/sys/dev/random/random_harvestq.c` is a readable example.

### Common beginner confusion

The most frequent trap is assuming that the thread running your driver code is a driver-owned thread. It is not. Most of the time it is a user thread that entered the kernel through a syscall, or an ithread that the interrupt framework created for you. Your driver only owns the threads it explicitly creates. Another recurring trap is reaching into `curthread->td_proc` from a context where `curthread` has nothing to do with the device (an interrupt ithread, for example); the process you find is not the process that asked for the operation.

### Where the book teaches this

Chapter 5 introduces kernel execution context and the sleep-versus-atomic distinction. Chapter 11 returns to it when concurrency becomes real. Chapter 14 uses taskqueues as a way to offload work into a safely sleepable context. Chapter 24 shows a full kproc lifecycle inside a driver.

### Further reading

- **In this book**: Chapter 11 (Concurrency in Drivers), Chapter 14 (Taskqueues and Deferred Work), Chapter 24 (Integrating with the Kernel).
- **Man pages**: `kthread(9)`, `kproc(9)`, `scheduler(9)`.
- **External**: McKusick, Neville-Neil, and Watson, *The Design and Implementation of the FreeBSD Operating System* (2nd ed.), chapters on process and thread management.

## Memory Subsystem

### What the subsystem is for

The virtual-memory (VM) subsystem manages every byte of memory the kernel can address. It owns the mapping from virtual addresses to physical pages, the allocation of pages to processes and to the kernel, the paging policy that reclaims pages under pressure, and the page-backing stores that hand pages in from disk, from devices, or from zero. A driver that allocates memory, exposes memory to userland through `mmap`, or does DMA is interacting with the VM subsystem whether it names it or not.

### Why a driver author should care

Four practical reasons. First, every kernel allocation goes through this subsystem, directly or indirectly. Second, any driver that exports a memory-mapped view of a device (or of a software buffer) does so through a VM pager. Third, DMA involves physical addresses, and only the VM subsystem knows how virtual kernel addresses translate to them. Fourth, the subsystem defines the sleep rules for allocation: `M_WAITOK` may walk the VM's page-reclamation path, which is something you cannot do from an interrupt filter.

### Key structures, interfaces, or concepts

- **`vm_map_t`** represents a contiguous collection of virtual address mappings belonging to one address space. The kernel has its own `vm_map_t`, and each user process has one. Drivers almost never walk a `vm_map_t` directly; higher-level APIs do the walking for them.
- **`vm_object_t`** represents a backing store: a set of pages that can be mapped into a `vm_map_t`. Objects are typed by the pager that produces their pages (anonymous, vnode-backed, swap-backed, device-backed).
- **`vm_page_t`** represents one physical page of RAM, together with its current state (wired, active, inactive, free) and the object it currently belongs to. All physical memory in the system is tracked by an array of `vm_page_t` records.
- **The pager layer** is the set of pluggable strategies that fill pages with data. The three most important ones for driver authors are the swap pager (anonymous memory), the vnode pager (file-backed memory), and the device pager (memory whose content is produced by a driver). When a driver implements `d_mmap` or `d_mmap_single`, it is publishing a slice of the device pager.
- **`pmap(9)`** is the machine-dependent page-table manager. It knows how to translate virtual addresses into physical ones for the current CPU architecture. Drivers rarely call `pmap` directly. The portable way to see a physical view is through `bus_dma(9)` (for DMA) or `bus_space(9)` (for MMIO registers).
- **UMA** is FreeBSD's slab allocator for fixed-size objects, with per-CPU caches to avoid locking in the fast path. `malloc(9)` itself is implemented on top of UMA for common sizes. Drivers that allocate and free millions of identical small objects per second (network descriptors, per-request contexts) create their own UMA zone with `uma_zcreate` and reuse objects instead of walking the general allocator.

### Typical driver touchpoints

- `malloc(9)`, `free(9)`, `contigmalloc(9)` for general control-plane memory.
- `uma_zcreate(9)`, `uma_zalloc(9)`, `uma_zfree(9)` for high-rate fixed-size objects.
- `bus_dmamem_alloc(9)` and the rest of the `bus_dma(9)` interface for DMA-capable memory; this is the driver-facing wrapper around the physical side of the VM.
- `d_mmap(9)` or `d_mmap_single(9)` in a `cdevsw` method table to publish a device pager view of hardware memory to userland.
- `vm_page_wire(9)` and `vm_page_unwire(9)` only in the rare cases where the driver needs to pin pages of a user buffer across a long-running I/O.

### Where to look in `/usr/src`

- `/usr/src/sys/vm/vm.h` declares the `vm_map_t`, `vm_object_t`, and `vm_page_t` typedefs.
- `/usr/src/sys/vm/vm_map.h`, `/usr/src/sys/vm/vm_object.h`, and `/usr/src/sys/vm/vm_page.h` hold the full type definitions.
- `/usr/src/sys/vm/swap_pager.c`, `/usr/src/sys/vm/vnode_pager.c`, and `/usr/src/sys/vm/device_pager.c` are the three pagers most relevant to drivers.
- `/usr/src/sys/vm/uma.h` is the UMA public interface; `/usr/src/sys/vm/uma_core.c` is the implementation.
- `/usr/src/sys/vm/pmap.h` is the machine-independent pmap interface; the machine-specific side lives under `/usr/src/sys/amd64/amd64/pmap.c`, `/usr/src/sys/arm64/arm64/pmap.c`, and similar files for each architecture.

### Manual pages and files to read next

`malloc(9)`, `uma(9)`, `contigmalloc(9)`, `bus_dma(9)`, `pmap(9)`, and the header `/usr/src/sys/vm/uma.h`. For a readable driver that publishes a device pager, inspect `/usr/src/sys/dev/drm2/` or the framebuffer code under `/usr/src/sys/dev/fb/`.

### Common beginner confusion

Two traps. First, conflating the three pointer flavours a DMA-capable driver sees: the kernel virtual address (what your pointer dereferences), the physical address (what the memory controller sees), and the bus address (what the device sees, which may go through an IOMMU). `bus_dma(9)` exists precisely to keep these separate. Second, assuming that a `bus_dmamem_alloc(9)` allocation is a generic memory allocation; it is a specialised allocation with stricter alignment, boundary, and segment rules dictated by the tag you passed in.

### Where the book teaches this

Chapter 5 introduces kernel memory and the allocator flags. Chapter 10 revisits buffers in read/write paths. Chapter 17 introduces `bus_space` for MMIO access. Chapter 21 is the full DMA chapter and where the bus-versus-physical distinction becomes concrete.

### Further reading

- **In this book**: Chapter 5 (Understanding C for FreeBSD Kernel Programming), Chapter 21 (DMA and High-Speed Data Transfer).
- **Man pages**: `malloc(9)`, `uma(9)`, `contigmalloc(9)`, `bus_dma(9)`, `pmap(9)`.
- **External**: McKusick, Neville-Neil, and Watson, *The Design and Implementation of the FreeBSD Operating System* (2nd ed.), chapter on memory management.

## File and VFS Subsystem

### What the subsystem is for

The file and VFS (Virtual File System) subsystem owns everything that userland sees through `open(2)`, `read(2)`, `write(2)`, `ioctl(2)`, `mmap(2)`, and the filesystem hierarchy in general. It dispatches operations to the correct filesystem through the vnode-operation vector, manages the buffer cache that sits between filesystems and storage drivers, and hosts the GEOM framework that lets storage drivers compose into stacks. For a driver author, this subsystem is either the main entry point (if you write a character or storage driver) or a quiet middle layer you are glad to let someone else worry about (if you write a network or embedded driver).

### Why a driver author should care

Three practical reasons. First, every character driver publishes itself to VFS through a `cdevsw` and a `/dev` node created by devfs. Second, every storage driver plugs into the bottom of a stack that the VFS and GEOM layers assemble on top, and the unit of work you receive is a `struct bio`, not a user pointer. Third, even a non-storage driver may need to understand vnodes and `mmap` if it publishes its memory to userland.

### Key structures, interfaces, or concepts

- **`struct vnode`** is the kernel's abstract file or device. It carries a type (regular file, directory, character device, block device, named pipe, socket, symbolic link), a pointer to its filesystem's `vop_vector`, the mount it belongs to, a reference count, and a lock. Every file descriptor in userland ultimately resolves to a vnode.
- **`struct vop_vector`** is the vnode-operation dispatch table: a pointer per operation (`VOP_LOOKUP`, `VOP_READ`, `VOP_WRITE`, `VOP_IOCTL`, and dozens more) that the filesystem or devfs implements. The vector is declared conceptually in `/usr/src/sys/sys/vnode.h` and generated from the operation list in `/usr/src/sys/kern/vnode_if.src`.
- **The GEOM framework** is FreeBSD's stackable storage layer. A GEOM *provider* is a storage surface; a *consumer* is something attaching to a provider. Drivers for storage hardware register as providers; classes such as `g_part`, `g_mirror`, or filesystems attach as consumers. The topology graph is dynamic and visible at runtime through `gpart show`, `geom disk list`, and `sysctl kern.geom`.
- **devfs** is the pseudo-filesystem that populates `/dev`. When your character driver calls `make_dev_s(9)`, devfs allocates a vnode-backed entry that forwards VFS operations into your `cdevsw` callbacks. devfs is the single layer between `open(2)` on a `/dev` path and `d_open` in your driver.
- **`struct buf`** is the traditional buffer-cache descriptor used by the old block-device path and by filesystems that layer on top of the buffer cache. It is still important because many filesystems drive I/O through `buf` objects before `buf_strategy()` funnels them into GEOM.
- **`struct bio`** is the modern per-operation descriptor that flows through GEOM. Every block read or write in GEOM is a `bio` with a command (`BIO_READ`, `BIO_WRITE`, `BIO_FLUSH`, `BIO_DELETE`), a range, a buffer pointer, and a completion callback. Your storage driver receives `bio`s on its start routine and calls `biodone()` (or the GEOM equivalent) when it finishes them.

### Typical driver touchpoints

- A character driver fills a `struct cdevsw` with callbacks (`d_open`, `d_close`, `d_read`, `d_write`, `d_ioctl`, optionally `d_poll`, `d_mmap`) and calls `make_dev_s(9)` to attach it to `/dev`.
- A storage driver registers a GEOM class, implements a start routine that accepts `bio`s, and calls `g_io_deliver()` when it finishes them.
- A driver that wants to be visible as a file (for reading telemetry, for example) may expose a character device whose `d_read` copies driver data out.
- A driver that publishes device memory to userland implements `d_mmap` or `d_mmap_single` to hand back a device pager object.

### Where to look in `/usr/src`

- `/usr/src/sys/sys/vnode.h` declares `struct vnode` and the vnode-operation plumbing.
- `/usr/src/sys/kern/vnode_if.src` is the source of truth for every VOP in the kernel; read it to see the operation list and the locking protocol.
- `/usr/src/sys/fs/devfs/` holds the devfs implementation; `devfs_devs.c` and `devfs_vnops.c` are the readable entry points.
- `/usr/src/sys/geom/geom.h` declares providers, consumers, and the GEOM class interface.
- `/usr/src/sys/sys/buf.h` and `/usr/src/sys/sys/bio.h` declare the block-I/O structures.
- `/usr/src/sys/dev/null/null.c` is the simplest character driver in the tree and the right first read.

### Manual pages and files to read next

`vnode(9)`, `VOP_LOOKUP(9)` and the rest of the VOP family, `devfs(4)`, `devfs(5)`, `cdev(9)`, `make_dev(9)`, `g_attach(9)`, `geom(4)`, and the header `/usr/src/sys/sys/bio.h`.

### Common beginner confusion

Three traps. First, expecting a character driver to deal with `struct buf` or `struct bio`. It does not; those live in the storage path. A character driver sees `struct uio` on its `d_read` and `d_write` callbacks and nothing more. Second, expecting a storage driver to create a `/dev` node itself. In modern FreeBSD the GEOM layer creates the `/dev` entries for block devices; your storage driver registers with GEOM, and devfs does the rest on the other side of GEOM. Third, assuming the vnode and the cdev are the same object. They are not. The vnode is the VFS-side handle to the open file; the cdev is the driver-side identity. `open(2)` on `/dev/foo` produces a vnode whose operations forward into your `cdevsw`.

### Where the book teaches this

Chapter 7 writes the first character driver and the first `cdevsw`. Chapter 8 walks through `make_dev_s(9)` and devfs node creation. Chapter 9 connects `d_read` and `d_write` to `uio`. Chapter 27 is the storage chapter and introduces `struct bio`, GEOM providers and consumers, and the buffer cache.

### Further reading

- **In this book**: Chapter 7 (Writing Your First Driver), Chapter 8 (Working with Device Files), Chapter 27 (Working with Storage Devices and the VFS Layer).
- **Man pages**: `vnode(9)`, `make_dev(9)`, `devfs(5)`, `geom(4)`, `g_bio(9)`.
- **External**: McKusick, Neville-Neil, and Watson, *The Design and Implementation of the FreeBSD Operating System* (2nd ed.), chapters on the I/O system and local filesystems.

## Network Subsystem

### What the subsystem is for

The network subsystem moves packets. It owns the data structures that represent a packet in flight (`mbuf` and friends), the per-interface state that represents a network device to the rest of the stack (`ifnet`), the routing tables that decide where a packet should go, the socket layer that userland sees, and the VNET infrastructure that lets multiple independent network stacks coexist in a single kernel. A network driver is the bottom layer of this stack: it hands packets up into the stack on receive, and the stack hands packets down into the driver on transmit.

### Why a driver author should care

Two reasons. If you write a network driver, nearly every byte you touch is a field of one of the structures named below, and the shape of your code is set by the protocols they enforce. If you write any other kind of driver, you still benefit from recognising `struct mbuf` and `struct ifnet` when you see them in code, because they turn up in many adjacent subsystems (packet filters, load-balancing helpers, virtual interfaces).

### Key structures, interfaces, or concepts

- **`struct mbuf`** is the packet fragment. Packets are represented as chains of mbufs, linked by `m_next` for a single packet and by `m_nextpkt` for consecutive packets in a queue. An mbuf carries a small header and either a small inline data area or a pointer into an external storage cluster. The design is optimised for cheap prepending of headers.
- **`struct m_tag`** is an extensible metadata tag attached to an mbuf. It lets the stack and drivers attach typed information to a packet (for example, hardware transmit-checksum offload, receive-side-scaling hash, filter decision) without enlarging the mbuf itself.
- **`ifnet`** (spelled `if_t` in modern APIs) is the per-interface descriptor. It carries the interface name and index, the flags, the MTU, a transmit function (`if_transmit`), the counters the stack increments, and the hooks that let higher layers deliver packets to the driver.
- **VNET** is the per-virtual-network-stack container. When `VIMAGE` is compiled in, every jail that enables VNET has its own routing table, its own set of interfaces, and its own protocol control blocks. Network drivers must be VNET-aware: they use `VNET_DEFINE` and `VNET_FOREACH` so that per-VNET state lives in the right place.
- **Routing** is the subsystem that selects a next-hop for an outbound packet. It owns the forwarding information base (FIB), a per-VNET radix tree of routes. Drivers rarely interact with routing directly; the stack has already chosen the interface before it reaches the driver.
- **The socket layer** is the kernel side of the `socket(2)` family of syscalls. For driver authors, the relevant fact is that a socket ultimately produces calls into `ifnet`, which produce calls into your driver. You do not implement sockets yourself.

### Typical driver touchpoints

- The driver allocates and populates an `ifnet` in `attach`, registers a transmit function, and calls `ether_ifattach(9)` or `if_attach(9)` to announce itself to the stack.
- The driver's transmit function receives an mbuf chain, writes descriptors, kicks the hardware, and returns.
- The receive path handles an interrupt or a poll, wraps received bytes in mbufs, and calls `if_input(9)` on the interface to push them up into the stack.
- On detach, the driver calls `ether_ifdetach(9)` or `if_detach(9)` before freeing its resources.
- The driver registers for `ifnet_arrival_event` or `ifnet_departure_event` if it needs to react when peers appear or disappear (see `/usr/src/sys/net/if_var.h` for the declarations).

### Where to look in `/usr/src`

- `/usr/src/sys/sys/mbuf.h` declares `struct mbuf` and `struct m_tag`.
- `/usr/src/sys/net/if.h` declares `if_t` and the public interface API.
- `/usr/src/sys/net/if_var.h` declares the interface-event eventhandlers and internal state.
- `/usr/src/sys/net/if_private.h` contains the full `struct ifnet` definition used inside the stack.
- `/usr/src/sys/net/vnet.h` declares the VNET infrastructure.
- `/usr/src/sys/net/route.h` and `/usr/src/sys/net/route/` hold the routing tables.
- `/usr/src/sys/sys/socketvar.h` declares `struct socket`.

### Manual pages and files to read next

`mbuf(9)`, `ifnet(9)`, `ether_ifattach(9)`, `vnet(9)`, `route(4)`, and `socket(9)`. For a small, readable real network driver, `/usr/src/sys/net/if_tuntap.c` is the canonical reading example.

### Common beginner confusion

Two traps. First, expecting a network driver to publish itself through `/dev`. It does not; it publishes itself through `ifnet` and becomes visible as `bge0`, `em0`, `igb0`, and so on, not through devfs. Second, holding onto an mbuf after handing it to the stack. Once you call `if_input` or return from `if_transmit`, the mbuf is no longer yours; using it afterwards corrupts the stack silently.

### Where the book teaches this

Chapter 28 is the full network-driver chapter and the right place for the details. Chapters 11 and 14 provide the locking and deferred-work discipline the receive path needs. Chapter 24 covers `ifnet_arrival_event` and related event hooks at a driver-integration level.

### Further reading

- **In this book**: Chapter 28 (Writing a Network Driver), Chapter 11 (Concurrency in Drivers), Chapter 14 (Taskqueues and Deferred Work).
- **Man pages**: `mbuf(9)`, `ifnet(9)`, `vnet(9)`, `socket(9)`.
- **External**: McKusick, Neville-Neil, and Watson, *The Design and Implementation of the FreeBSD Operating System* (2nd ed.), chapters on the network subsystem.

## Driver Infrastructure (Newbus)

### What the subsystem is for

Newbus is FreeBSD's driver framework. It owns the tree of devices in the system, matches drivers to devices through probe, manages the lifecycle of each attachment, routes resource allocations to the right bus, and provides the object-oriented dispatch that lets buses override and extend each other's behaviour. Every character driver, storage driver, network driver, and embedded driver in the tree is a Newbus participant. If the other subsystems in this appendix are the rooms, Newbus is the hallway that connects them.

### Why a driver author should care

There is essentially no FreeBSD driver without Newbus. The types you meet first (`device_t`, softc, `driver_t`, `devclass_t`), the APIs you reach for in `attach` (`bus_alloc_resource_any`, `bus_setup_intr`), the macros you wrap the whole driver in (`DRIVER_MODULE`, `DEVMETHOD`, `DEVMETHOD_END`) all belong here. Learning to navigate Newbus is the same thing as learning to navigate FreeBSD driver source.

### Key structures, interfaces, or concepts

- **`device_t`** is an opaque handle to a node in the Newbus device tree. You receive one in `probe` and `attach`, pass it into almost every bus API, and use it to fetch the softc with `device_get_softc(9)`.
- **`driver_t`** is the descriptor for a driver: its name, its method table, and the size of its softc. You construct one for your driver and hand it to `DRIVER_MODULE(9)`, which registers it under a parent bus name.
- **`devclass_t`** is the per-driver-class registry: the collection of `device_t` instances a driver has attached. It is how the kernel gives each instance a unit number.
- **`kobj(9)`** is the object-oriented machinery underneath Newbus. Method tables, method dispatch, and the ability for a bus to inherit another bus's methods are all kobj features. As a driver author you use the `DEVMETHOD` macros, which expand into kobj metadata; you rarely call kobj primitives directly.
- **softc** is the per-instance state of your driver, allocated by the kernel when it allocates the `device_t`. The kernel knows how large your softc is because you told it in the `driver_t`. `device_get_softc(9)` gives you back a pointer to it.
- **`bus_alloc_resource(9)`** and friends allocate memory windows, I/O ports, and interrupt lines from the parent bus on behalf of your driver. They are the portable way to get at a device's resources without caring which bus it sits on.

### Typical driver touchpoints

- Declare a `device_method_t` array with `DEVMETHOD(device_probe, ...)`, `DEVMETHOD(device_attach, ...)`, `DEVMETHOD(device_detach, ...)`, terminated with `DEVMETHOD_END`.
- Declare a `driver_t` with your driver name, the methods, and `sizeof(struct mydev_softc)`.
- Use `DRIVER_MODULE(mydev, pci, mydev_driver, ...)` (or `usbus`, `iicbus`, `spibus`, `simplebus`, `acpi`, `nexus`) to register the driver under its parent bus.
- In `probe`, decide whether this device is yours and, if so, return `BUS_PROBE_DEFAULT` (or a weaker/stronger value) and a description.
- In `attach`, allocate resources with `bus_alloc_resource_any(9)`, map registers with `bus_space(9)`, set up interrupts with `bus_setup_intr(9)`, and only then expose yourself to the rest of the kernel.
- In `detach`, undo everything in reverse order.

### Where to look in `/usr/src`

- `/usr/src/sys/sys/bus.h` declares `device_t`, `driver_t`, `devclass_t`, `DEVMETHOD`, `DEVMETHOD_END`, `DRIVER_MODULE`, `bus_alloc_resource_any`, `bus_setup_intr`, and most of the rest.
- `/usr/src/sys/sys/kobj.h` declares the method-dispatch machinery.
- `/usr/src/sys/kern/subr_bus.c` holds the Newbus implementation.
- `/usr/src/sys/kern/subr_kobj.c` holds the kobj implementation.
- `/usr/src/sys/dev/null/null.c` and `/usr/src/sys/dev/led/led.c` are very small real drivers you can read in a sitting.

### Manual pages and files to read next

`device(9)`, `driver(9)`, `DEVMETHOD(9)`, `DRIVER_MODULE(9)`, `bus_alloc_resource(9)`, `bus_setup_intr(9)`, `kobj(9)`, and `devinfo(8)` to see the running Newbus tree.

### Common beginner confusion

Two traps. First, thinking that `device_t` and softc are the same object. `device_t` is the Newbus handle; softc is your driver's private state. You fetch softc from `device_t` with `device_get_softc(9)`. Second, forgetting that `DRIVER_MODULE`'s second argument is the parent bus name. A driver declared with `DRIVER_MODULE(..., pci, ...)` can only attach under a PCI bus, no matter how many PCI-like boards exist elsewhere. If a driver must attach under multiple buses (for example, a chip that appears both as PCI and as ACPI), you register it twice.

### Where the book teaches this

Chapter 6 is the full anatomy of a driver chapter and the canonical teaching location for everything above. Chapter 7 writes the first working driver against these APIs. Chapter 18 extends the picture for PCI. Chapter 24 returns to `DRIVER_MODULE`, `MODULE_VERSION`, and `MODULE_DEPEND` when kernel integration becomes the subject.

### Further reading

- **In this book**: Chapter 6 (The Anatomy of a FreeBSD Driver), Chapter 7 (Writing Your First Driver), Chapter 18 (Writing a PCI Driver).
- **Man pages**: `device(9)`, `driver(9)`, `DRIVER_MODULE(9)`, `bus_alloc_resource(9)`, `bus_setup_intr(9)`, `kobj(9)`, `rman(9)`.

## Boot and Module System

### What the subsystem is for

The boot and module system is how the kernel gets into memory, how it initialises the hundreds of subsystems it depends on before anything can run, and how code that is not compiled into the kernel (loadable modules) is brought in, wired up, and eventually removed. From a driver author's perspective, the subsystem defines when your initialisation code runs relative to the rest of the kernel, and how your module-level `MOD_LOAD` and `MOD_UNLOAD` events interact with the kernel's internal initialisation order.

### Why a driver author should care

Three reasons. First, if your driver can be loaded as a module, it may run on a kernel whose subsystems have been ordered differently than you expect, and you need to declare what you depend on. Second, if your driver must run early (for example, a console driver or a boot-time storage driver), you must understand `SYSINIT(9)` subsystem IDs so that your code runs in the right slot. Third, even a plain driver relies on the module system to register itself, to declare ABI compatibility, and to fail cleanly if a dependency is missing.

### Key structures, interfaces, or concepts

- **The boot sequence** follows a fixed arc: the loader reads the kernel from disk, hands control to the kernel entry point, which sets up the early CPU state and then calls `mi_startup()`. `mi_startup()` walks a sorted list of `SYSINIT` entries, invoking each one in order. When the list is exhausted, the kernel has enough services up to start `init(8)` as user process 1.
- **`SYSINIT(9)`** is the macro that registers a function to be called at a specific phase of kernel initialisation. Each entry has a subsystem ID (`SI_SUB_*`, coarse ordering) and an order-within-subsystem (`SI_ORDER_*`, fine ordering). The full list of legal subsystem IDs is in `/usr/src/sys/sys/kernel.h` and is worth skimming once. `SYSUNINIT(9)` is the matching teardown.
- **Module loading** is driven by the KLD framework. `kldload(8)` invokes the linker, which relocates the module, resolves its symbols against the running kernel, and invokes the module's event handler with `MOD_LOAD`. A matching `MOD_UNLOAD` runs when the module is removed. Drivers rarely write module event handlers by hand; `DRIVER_MODULE(9)` produces one for you.
- **`MODULE_DEPEND(9)`** declares that your module requires another module (`usb`, `miibus`, `pci`, `iflib`) to be present, and at which version range. The kernel refuses to load your module if the dependency is missing.
- **`MODULE_VERSION(9)`** declares the ABI version your module exports, so that other modules can depend on it with `MODULE_DEPEND`.

### Typical driver touchpoints

- `DRIVER_MODULE(mydev, pci, mydev_driver, ...)` emits a module event handler that registers the driver on `MOD_LOAD` and deregisters it on `MOD_UNLOAD`.
- `MODULE_VERSION(mydev, 1);` announces your module's ABI version.
- `MODULE_DEPEND(mydev, pci, 1, 1, 1);` declares a pci dependency.
- A driver that must run before Newbus is available uses `SYSINIT(9)` to register a one-off setup hook at an early subsystem ID.
- A driver that wires a teardown hook at the last possible moment uses `SYSUNINIT(9)` with the matching ordering.

### Where to look in `/usr/src`

- `/usr/src/sys/sys/kernel.h` defines `SYSINIT`, `SYSUNINIT`, `SI_SUB_*`, and `SI_ORDER_*`.
- `/usr/src/sys/kern/init_main.c` contains `mi_startup()` and the walk through the SYSINIT list.
- `/usr/src/sys/sys/module.h` declares `MODULE_VERSION` and `MODULE_DEPEND`.
- `/usr/src/sys/sys/linker.h` and `/usr/src/sys/kern/kern_linker.c` implement the KLD linker.
- `/usr/src/stand/` holds the loader and the boot-time code. (On older FreeBSD releases this lived under `/usr/src/sys/boot/`; FreeBSD 14 hosts it entirely under `/usr/src/stand/`.)

### Manual pages and files to read next

`SYSINIT(9)`, `kld(9)`, `kldload(9)`, `kldload(8)`, `kldstat(8)`, `module(9)`, `MODULE_VERSION(9)`, and `MODULE_DEPEND(9)`. For a short real `SYSINIT` example, look near the top of `/usr/src/sys/dev/random/random_harvestq.c`.

### Common beginner confusion

Two traps. First, assuming that `MOD_LOAD` is the moment your `attach` function runs. It is not. `MOD_LOAD` is the moment your *driver* is registered with Newbus; `attach` runs later, per device, whenever a bus offers a matching child. Second, using `SYSINIT` levels as if they were arbitrary. Each `SI_SUB_*` corresponds to a well-defined phase of kernel startup, and registering your hook at the wrong phase either makes it run too early (with half the kernel missing) or too late (after the event you cared about has passed).

### Where the book teaches this

Chapter 6 introduces `DRIVER_MODULE`, `MODULE_VERSION`, and `MODULE_DEPEND` as part of driver anatomy. Chapter 24 covers kernel-integration topics including `SYSINIT`, subsystem IDs, and module teardown ordering. Chapter 32 returns to boot-time concerns on embedded platforms.

### Further reading

- **In this book**: Chapter 24 (Integrating with the Kernel), Chapter 32 (Device Tree and Embedded Development).
- **Man pages**: `SYSINIT(9)`, `module(9)`, `MODULE_VERSION(9)`, `MODULE_DEPEND(9)`, `kldload(8)`, `kldstat(8)`.

## Kernel Services

### What the subsystem is for

The kernel ships a small set of general-purpose services that are not tied to any one subsystem but show up repeatedly in drivers: event notifications, deferred-work queues, timed callbacks, and subscription hooks. None of them teaches you how to write a driver, but all of them turn up in real driver code, and recognising them speeds up every code-reading session. This section collects the handful you are likely to meet.

### Why a driver author should care

Drivers often need to react to system-wide events (shutdown, low memory, interface arrival, root filesystem mount), or to do work outside the context that delivered the event (away from an interrupt filter, away from a spin-locked critical section). The kernel services below are the standard FreeBSD answers to both needs. Using them means your driver integrates cleanly with the rest of the system; reimplementing them means you will eventually collide with a subsystem that expects your hooks to exist.

### Key structures, interfaces, or concepts

- **`eventhandler(9)`** is the publish/subscribe system for kernel events. A publisher declares an event with `EVENTHANDLER_DECLARE`, a subscriber registers with `EVENTHANDLER_REGISTER`, and invocation with `EVENTHANDLER_INVOKE` fans out to every subscriber. Standard event tags defined in `/usr/src/sys/sys/eventhandler.h` include `shutdown_pre_sync`, `shutdown_post_sync`, `shutdown_final`, `vm_lowmem`, and `mountroot`; interface events (`ifnet_arrival_event`, `ifnet_departure_event`) are declared in `/usr/src/sys/net/if_var.h`. Drivers use these to clean up, to release memory, to react when a sibling interface appears, or to delay early work until the root filesystem is available.
- **`taskqueue(9)`** is a queue of deferred work items. A driver enqueues a task from a context that cannot sleep (for example, an interrupt filter) and the task later runs on a dedicated worker thread where sleeping and blocking are allowed. The kernel ships a small set of system-wide taskqueues (`taskqueue_swi`, `taskqueue_thread`, `taskqueue_fast`) and lets you create your own.
- **Grouped taskqueues (`gtaskqueue`)** extend `taskqueue` with CPU affinity and rebalancing; they are heavily used in `iflib` and in the high-rate network stack. The declarations live in `/usr/src/sys/sys/gtaskqueue.h`.
- **`callout(9)`** is the kernel's one-shot and periodic timer. A driver arms a callout with a future deadline and receives a callback when the deadline arrives. `callout(9)` replaces almost every ad-hoc "sleep for N ticks" loop a driver might otherwise write.
- **`hooks(9)`-style subsystem extension points.** A number of FreeBSD subsystems publish registration APIs that behave like eventhandlers but are specific to the subsystem (for example, packet filters register with `pfil(9)`; disk drivers can register with `disk(9)` events). These are not one unified interface, but the pattern is the same: a list of callbacks that a subsystem invokes at a well-defined moment.

### Typical driver touchpoints

- `EVENTHANDLER_REGISTER(shutdown_pre_sync, mydev_shutdown, softc, SHUTDOWN_PRI_DEFAULT);` in `attach` so the driver flushes hardware before a reboot; `EVENTHANDLER_DEREGISTER` in `detach`. (The three standard priority constants for shutdown hooks are `SHUTDOWN_PRI_FIRST`, `SHUTDOWN_PRI_DEFAULT`, and `SHUTDOWN_PRI_LAST`, declared in `/usr/src/sys/sys/eventhandler.h`.)
- `taskqueue_create("mydev", M_WAITOK, ...); taskqueue_start_threads(...);` in `attach` to create a per-device worker; `taskqueue_drain_all` and `taskqueue_free` in `detach`.
- `callout_init_mtx(&sc->sc_watchdog, &sc->sc_mtx, 0)` in `attach` to arm a watchdog; `callout_drain` in `detach`.
- Grouped taskqueues are most visible inside `iflib`-based network drivers; a typical standalone driver rarely reaches for them directly.

### Where to look in `/usr/src`

- `/usr/src/sys/sys/eventhandler.h` and `/usr/src/sys/kern/subr_eventhandler.c` for event handlers.
- `/usr/src/sys/sys/taskqueue.h` and `/usr/src/sys/kern/subr_taskqueue.c` for taskqueues.
- `/usr/src/sys/sys/gtaskqueue.h` and `/usr/src/sys/kern/subr_gtaskqueue.c` for grouped taskqueues.
- `/usr/src/sys/sys/callout.h` and `/usr/src/sys/kern/kern_timeout.c` for callouts.

### Manual pages and files to read next

`eventhandler(9)`, `taskqueue(9)`, `callout(9)`, and the header `/usr/src/sys/sys/eventhandler.h`. Look at `/usr/src/sys/dev/random/random_harvestq.c` for a driver that uses `SYSINIT` and a dedicated kproc cleanly; it is a good companion when reading about kernel services even though it does not itself exercise `taskqueue(9)` or `callout(9)`.

### Common beginner confusion

One important trap: forgetting that registration is half of the contract. Every `EVENTHANDLER_REGISTER` needs an `EVENTHANDLER_DEREGISTER` at the matching lifecycle moment, every `taskqueue_create` needs a `taskqueue_free`, and every armed `callout` needs a `callout_drain` before its memory is freed. A leaked registration keeps a dangling pointer into freed memory; the next invocation of the event will then crash the kernel in a subsystem that has nothing to do with your driver.

### Where the book teaches this

Chapter 13 introduces `callout(9)`. Chapter 14 is the taskqueue chapter. Chapter 24 is the kernel-integration chapter and covers `eventhandler(9)` and the SYSINIT/module cooperation in context.

### Further reading

- **In this book**: Chapter 13 (Timers and Delayed Work), Chapter 14 (Taskqueues and Deferred Work), Chapter 24 (Integrating with the Kernel).
- **Man pages**: `eventhandler(9)`, `taskqueue(9)`, `callout(9)`.

## Cross-References: Structures and Their Subsystems

The table below is the fastest way to turn an unfamiliar type into a known subsystem. Use it when you are reading driver source, hit a struct name you do not recognise, and want to know which section of this appendix to open.

| Structure or type         | Subsystem                     | Where declared                                     |
| :------------------------ | :---------------------------- | :------------------------------------------------- |
| `struct proc`, `thread`   | Process and Thread            | `/usr/src/sys/sys/proc.h`                          |
| `vm_map_t`                | Memory (VM)                   | `/usr/src/sys/vm/vm.h` and `/usr/src/sys/vm/vm_map.h` |
| `vm_object_t`             | Memory (VM)                   | `/usr/src/sys/vm/vm.h` and `/usr/src/sys/vm/vm_object.h` |
| `vm_page_t`               | Memory (VM)                   | `/usr/src/sys/vm/vm.h` and `/usr/src/sys/vm/vm_page.h` |
| `uma_zone_t`              | Memory (VM)                   | `/usr/src/sys/vm/uma.h`                            |
| `struct vnode`            | File and VFS                  | `/usr/src/sys/sys/vnode.h`                         |
| `struct vop_vector`       | File and VFS                  | generated from `/usr/src/sys/kern/vnode_if.src`    |
| `struct buf`              | File and VFS                  | `/usr/src/sys/sys/buf.h`                           |
| `struct bio`              | File and VFS (GEOM)           | `/usr/src/sys/sys/bio.h`                           |
| `struct g_provider`       | File and VFS (GEOM)           | `/usr/src/sys/geom/geom.h`                         |
| `struct cdev`             | File and VFS (devfs)          | `/usr/src/sys/sys/conf.h`                          |
| `struct cdevsw`           | File and VFS (devfs)          | `/usr/src/sys/sys/conf.h`                          |
| `struct mbuf`, `m_tag`    | Network                       | `/usr/src/sys/sys/mbuf.h`                          |
| `if_t`, `struct ifnet`    | Network                       | `/usr/src/sys/net/if.h`, `/usr/src/sys/net/if_private.h` |
| `struct socket`           | Network                       | `/usr/src/sys/sys/socketvar.h`                     |
| `device_t`                | Driver Infrastructure         | `/usr/src/sys/sys/bus.h`                           |
| `driver_t`, `devclass_t`  | Driver Infrastructure         | `/usr/src/sys/sys/bus.h`                           |
| `device_method_t`         | Driver Infrastructure (kobj)  | `/usr/src/sys/sys/bus.h` (kobj in `sys/kobj.h`)    |
| `struct resource`         | Driver Infrastructure         | `/usr/src/sys/sys/rman.h`                          |
| `SYSINIT`, `SI_SUB_*`     | Boot and Module               | `/usr/src/sys/sys/kernel.h`                        |
| `MODULE_VERSION`, `MODULE_DEPEND` | Boot and Module       | `/usr/src/sys/sys/module.h`                        |
| `eventhandler_tag`        | Kernel Services               | `/usr/src/sys/sys/eventhandler.h`                  |
| `struct taskqueue`        | Kernel Services               | `/usr/src/sys/sys/taskqueue.h`                     |
| `struct callout`          | Kernel Services               | `/usr/src/sys/sys/callout.h`                       |

When a type is not in the table, search `/usr/src/sys/sys/` or `/usr/src/sys/<subsystem>/` for its declaration; the comment near the definition usually names the subsystem outright.

## Source-Tree Navigation Checklists

The FreeBSD source tree is organised by responsibility, and once you know the pattern you can guess where almost anything lives. The lists below are the five quick questions that turn "where in the tree" into "open this file".

### When you have a structure name

1. Is it a low-level primitive (`proc`, `thread`, `vnode`, `buf`, `bio`, `mbuf`, `callout`, `taskqueue`, `eventhandler`)? Look in `/usr/src/sys/sys/` first.
2. Is it a VM type (`vm_*`, `uma_*`)? Look in `/usr/src/sys/vm/`.
3. Is it a network type (`ifnet`, `if_*`, `m_tag`, `route`, `socket`, `vnet`)? Look in `/usr/src/sys/net/`, `/usr/src/sys/netinet/`, or `/usr/src/sys/netinet6/`.
4. Is it a device or bus type (`device_t`, `driver_t`, `resource`, `rman`, `pci_*`, `usbus_*`)? Look in `/usr/src/sys/sys/bus.h`, `/usr/src/sys/sys/rman.h`, or the matching bus directory under `/usr/src/sys/dev/`.
5. Is it something else entirely? `grep -r 'struct NAME {' /usr/src/sys/sys/ /usr/src/sys/kern/ /usr/src/sys/vm/ /usr/src/sys/net/` usually finds it in one pass.

### When you have a function name

1. If the name starts with `vm_`, it lives under `/usr/src/sys/vm/`.
2. If it starts with `bus_`, `device_`, `driver_`, `devclass_`, `resource_`, it lives in `/usr/src/sys/kern/subr_bus.c`, `/usr/src/sys/kern/subr_rman.c`, or one of the bus-specific directories.
3. If it starts with `vfs_`, `vn_`, or a `VOP_` prefix, it lives under `/usr/src/sys/kern/vfs_*.c` or one of the filesystems under `/usr/src/sys/fs/`.
4. If it starts with `g_`, it is GEOM; look in `/usr/src/sys/geom/`.
5. If it starts with `if_`, `ether_`, or `in_`, it is networking; look in `/usr/src/sys/net/` or `/usr/src/sys/netinet/`.
6. If it starts with `kthread_`, `kproc_`, `sched_`, or `proc_`, it is the process/thread subsystem under `/usr/src/sys/kern/`.
7. If it starts with `uma_` or `malloc`, it is memory; look in `/usr/src/sys/vm/uma_core.c` or `/usr/src/sys/kern/kern_malloc.c`.
8. When nothing matches, `grep -rl '\bFUNC_NAME\s*(' /usr/src/sys/` is slower but exhaustive.

### When you have a macro name

1. `SYSINIT`, `SYSUNINIT`, `SI_SUB_*`, `SI_ORDER_*`: `/usr/src/sys/sys/kernel.h`.
2. `DRIVER_MODULE`, `DEVMETHOD`, `DEVMETHOD_END`, `MODULE_VERSION`, `MODULE_DEPEND`: `/usr/src/sys/sys/bus.h` and `/usr/src/sys/sys/module.h`.
3. `EVENTHANDLER_*`: `/usr/src/sys/sys/eventhandler.h`.
4. `VNET_*`, `CURVNET_*`: `/usr/src/sys/net/vnet.h`.
5. `TAILQ_*`, `LIST_*`, `STAILQ_*`, `SLIST_*`: `/usr/src/sys/sys/queue.h`.
6. `VOP_*`: generated from `/usr/src/sys/kern/vnode_if.src`, visible in `sys/vnode_if.h` once the kernel is built.

### When you have a subsystem question

1. What initialises the kernel and in which order? `/usr/src/sys/kern/init_main.c`.
2. What drivers does the tree contain? `ls /usr/src/sys/dev/` and its subdirectories.
3. Where are network-stack entry points? `/usr/src/sys/net/if.c`, `/usr/src/sys/netinet/`, and their siblings.
4. How does a specific syscall reach a driver? start in `/usr/src/sys/kern/syscalls.master`, follow the dispatcher into the relevant VFS or socket code, and keep reading until the dispatch lands in a `cdevsw`, a `vop_vector`, or an `ifnet`.

## Manual Pages and Source Reading Itinerary

Pattern recognition across the kernel comes from reading it, not only from reading about it. A self-study plan that covers the subsystems in this appendix might look like this:

1. `intro(9)` plus a walk of `/usr/src/sys/sys/` file names, fifteen minutes total.
2. `kthread(9)`, `kproc(9)`, and `/usr/src/sys/sys/proc.h`.
3. `malloc(9)`, `uma(9)`, `bus_dma(9)`, and `/usr/src/sys/vm/uma.h`.
4. `vnode(9)`, `cdev(9)`, `make_dev(9)`, `devfs(4)`, and `/usr/src/sys/dev/null/null.c`.
5. `mbuf(9)`, `ifnet(9)`, `ether_ifattach(9)`, and `/usr/src/sys/net/if_tuntap.c`.
6. `device(9)`, `DRIVER_MODULE(9)`, `bus_alloc_resource(9)`, and `/usr/src/sys/dev/led/led.c`.
7. `SYSINIT(9)`, `kld(9)`, `module(9)`, and the top of `/usr/src/sys/kern/init_main.c`.
8. `eventhandler(9)`, `taskqueue(9)`, `callout(9)`, and `/usr/src/sys/dev/random/random_harvestq.c`.

The companion files in `examples/appendices/appendix-e-navigating-freebsd-kernel-internals/` collect the same itinerary in a form you can print, annotate, and keep next to the machine.

## Wrapping Up: How to Keep Exploring the Kernel Safely

Exploring a kernel source tree can feel endless, and it is easy to lose a weekend chasing one interesting thread through ten subsystems. A small set of habits keeps the exploration productive.

Read in short sessions with a specific question. "What does `bus_setup_intr` actually do under the hood" is a good session. "Read the VM" is not.

Keep the map in view. When you jump from a driver into the VFS, remind yourself that you are now in the VFS and that the rules of the VFS apply. When you return to the driver, remind yourself that the VFS stopped at the function boundary. Each subsystem has its own invariants and its own locking discipline, and they rarely carry over.

Write down what you find. A short note like "`bus_alloc_resource_any` in subr_bus.c calls into `BUS_ALLOC_RESOURCE` via kobj dispatch, which the PCI bus method implements in `pci.c`" is worth more than an afternoon of passive reading. The appendix and its companion files are there to give you anchor points for exactly this kind of note-taking.

Use the safety rails. `/usr/src/sys/dev/null/null.c` and `/usr/src/sys/dev/led/led.c` are tiny. `/usr/src/sys/net/if_tuntap.c` is small enough to read in a sitting. `/usr/src/sys/dev/random/random_harvestq.c` uses real kernel services without hiding behind layers of abstraction. Start from these whenever a subsystem feels too large to approach directly.

And remember that the goal is not to memorise the kernel. It is to build enough pattern recognition that, the next time you open an unfamiliar driver or a new subsystem, the structures, the functions, and the source paths feel like neighbourhoods you have already walked through. This appendix, together with Appendices A through D and the chapters that teach the pieces in context, is designed to make that feeling arrive sooner.

When the map here is not enough, the book is. When the book is not enough, the source is. And the source is already sitting on your FreeBSD machine, waiting to be read.
