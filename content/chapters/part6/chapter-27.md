---
title: "Working with Storage Devices and the VFS Layer"
description: "Developing storage device drivers and VFS integration"
partNumber: 6
partName: "Writing Transport-Specific Drivers"
chapter: 27
lastUpdated: "2026-04-19"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "TBD"
estimatedReadTime: 225
---

# Working with Storage Devices and the VFS Layer

## Introduction

In the previous chapter, we walked carefully through the life of a USB serial driver. We followed the device from the moment the kernel noticed it on the bus, through probe and attach, into its active life as a character device, and finally out through detach when the hardware was unplugged. That walkthrough taught us how transport-specific drivers live inside FreeBSD. They participate in a bus, they expose a user-facing abstraction, and they accept that they may vanish at any moment because the underlying hardware is removable.

Storage drivers live in a very different country. The hardware is still real, and many storage devices can still be removed unexpectedly, but the role of the driver shifts in an important way. A USB serial adapter offers a stream of bytes to one process at a time. A storage device offers a block-addressable, long-lived, structured surface on which filesystems are built. When a user plugs in a USB serial adapter, they might immediately open `/dev/cuaU0` and begin a session. When a user plugs in a disk, they almost never read it as a raw stream. They mount it, and from that moment on the disk disappears behind a filesystem, behind a cache, behind the Virtual File System layer, and behind the many processes that share files on it.

This chapter teaches you what happens on the driver side of that arrangement. You will learn what the VFS layer is, how it differs from `devfs`, and how storage drivers plug into the GEOM framework rather than speaking to the VFS layer directly. You will write a small pseudo block device from scratch, expose it as a GEOM provider, give it a working backing store, watch `newfs_ufs` format it, mount the result, create files on it, unmount it cleanly, and detach it without leaving footprints in the kernel. By the end of the chapter you will have a working mental model of the storage stack and a concrete example driver that exercises every layer we discuss.

The chapter is long because the topic is layered. Unlike a character driver, where the main unit of interaction is a single `read` or `write` call from a process, a storage driver lives inside a chain of frameworks. Requests travel from a process through VFS, through the buffer cache, through the filesystem, through GEOM, and only then reach the driver. Replies travel back the other way. Understanding that chain is essential before writing any real storage code, and it is essential again when diagnosing the kind of subtle failures that appear only under load or during unmount. We will move slowly through the foundations, then gradually bring more layers into view.

As with Chapter 26, the goal here is not to ship a production block driver. The goal is to give you a sturdy, correct, readable first block driver that you understand completely. Real production storage drivers, for SATA disks, NVMe drives, SCSI controllers, SD cards, or virtual block devices, build on the same patterns. Once the foundations are clear, the step from pseudo to real is mostly a matter of replacing the backing store with code that talks to hardware registers and DMA engines, and of handling the much richer error and recovery surface that real disks expose.

You will also see how storage drivers interact with tools the reader already knows from the user side of FreeBSD. `mdconfig(8)` will appear as a close cousin of our driver, since the kernel's `md(4)` RAM disk is exactly the kind of thing we are building. `newfs_ufs(8)`, `mount(8)`, `umount(8)`, `diskinfo(8)`, `gstat(8)`, and `geom(8)` will become tools of verification, not just tools that other people use. The chapter is structured so that by the time you finish, you can look at the output of `gstat -I 1` while running `dd` against your device and read it with understanding.

Finally, a note on what we will not cover here. We will not write a real bus driver that talks to a physical storage controller. We will not discuss the internals of UFS, ZFS, FUSE, or other specific filesystems beyond what is needed to understand how they meet a block device at the boundary. We will not cover DMA, PCIe, NVMe queues, or SCSI command sets. All of those topics deserve their own treatment and, where relevant, will appear in later chapters that cover specific buses and specific subsystems. What we will do here is give you a complete, self-contained block-layer experience that is representative of how all storage drivers in FreeBSD integrate with the kernel.

Take your time with this chapter. Read slowly, type the code, boot the module, format it, mount it, break it on purpose, watch what happens. The storage stack rewards patience and punishes shortcuts. You are not in a race.

## How Part 6 Differs from Parts 1 Through 5

A short framing note before the chapter begins. Chapter 27 sits inside a Part that asks you to change one specific habit, and that shift is easier to make when it is named up front.

Parts 1 through 5 built a single running driver, `myfirst`, through twenty consecutive chapters, each adding one discipline to the same source tree. Chapter 26 extended that family with `myfirst_usb` as a transport sibling so the step into real hardware would not also be a step into unfamiliar source. **From Chapter 27 onward, the running `myfirst` driver pauses as the backbone of the book.** Part 6 shifts to new, self-contained demos that fit each subsystem it teaches: a pseudo block device for storage here in Chapter 27, and a pseudo network interface for networking in Chapter 28. These demos are parallel to `myfirst` in spirit but distinct in code, because the patterns that define a storage driver or a network driver do not fit the character-device mould that `myfirst` grew from.

The **discipline and didactic shape continue unchanged**. Each chapter still walks you through probe, attach, the primary data path, the cleanup path, labs, challenge exercises, troubleshooting, and a bridge to the next chapter. Each chapter still grounds its examples in real FreeBSD source under `/usr/src`. The habits you built in Chapters 25 and earlier, the labelled-goto cleanup chain, rate-limited logging, `INVARIANTS` and `WITNESS`, the production-readiness checklist, carry over without modification. What changes is the code artefact in front of you: a small, focused driver whose shape matches the subsystem under study, rather than one more stage in the `myfirst` timeline.

This is a deliberate didactic choice, not an accident of scope. A storage driver and a network driver each have their own lifecycle, their own data flow, their own preferred idioms, and their own frameworks to plug into. Teaching them as fresh drivers, rather than as further mutations of `myfirst`, keeps the focus on what makes each subsystem distinctive. A reader who tries to stretch `myfirst` into a block device or a network interface quickly ends up with code that teaches nothing about storage or networking. Fresh demos are the cleaner path, and they are the path this Part takes.

Part 7 returns to cumulative learning, but rather than resuming a single running driver, it revisits the drivers you have already written (`myfirst`, `myfirst_usb`, and the Part 6 demos) and teaches the production-minded topics that matter once a first version of a driver exists: portability across architectures, advanced debugging, performance tuning, security review, and contribution to the upstream project. The habit of building cumulatively stays with you; only the specific artefact in front of you changes.

Keep this framing in mind as Chapter 27 unfolds. If the switch from `myfirst` to a new pseudo block device feels jarring after twenty chapters of the same source tree, that reaction is expected and passes quickly, usually by the end of Section 3.

## Reader Guidance: How to Use This Chapter

This chapter is designed as a guided course through the storage side of the FreeBSD kernel. It is one of the longer chapters in the book because the subject matter is layered and every layer has its own vocabulary, its own concerns, and its own failure modes. You do not need to rush through it.

If you choose the **reading-only path**, expect to spend around two to three hours going through the chapter carefully. You will come away with a clear picture of how VFS, the buffer cache, filesystems, GEOM, and the block device boundary fit together, and you will have a concrete driver in front of you as an anchor for your mental model. This is a legitimate way to use the chapter, especially on a first pass.

If you choose the **reading-plus-labs path**, plan for four to six hours spread across one or two evenings, depending on your comfort with kernel modules from Chapter 26. You will build the driver, format it, mount it, watch it under load, and take it apart safely. Expect the mechanics of `kldload`, `kldunload`, `newfs_ufs`, and `mount` to become second nature by the end.

If you choose the **reading-plus-labs-plus-challenges path**, plan for a weekend or two evenings spread over a week. The challenges extend the driver in small directions that matter in practice: adding optional flush semantics, responding to `BIO_DELETE` with zeroing, supporting multiple units, exporting extra attributes through `disk_getattr`, and enforcing read-only mode cleanly. Each challenge is self-contained and uses only what the chapter has already covered.

Whichever path you choose, do not skip the troubleshooting section. Storage bugs tend to look alike from the outside, and the ability to recognise them by symptom is far more useful in practice than memorising the names of every function in GEOM. The troubleshooting material is placed near the end for readability, but you may find yourself turning back to it while working through the labs.

A word on prerequisites. This chapter builds directly on Chapter 26, so at minimum you should be comfortable writing a small kernel module, declaring a softc, allocating and freeing resources, and walking through the load and unload path. You should also be comfortable enough with the shell to run `kldload`, `kldstat`, `dmesg`, `mount`, and `umount` without stopping to look up flags. If any of that feels unfamiliar, it is worth revisiting Chapters 5, 14, and 26 before continuing.

You should work on a throwaway FreeBSD 14.3 system, a virtual machine, or a branch where you do not mind the occasional kernel panic. A panic is unlikely if you follow the text carefully, but the cost of a mistake on your development laptop is much higher than the cost of a mistake on a VM snapshot you can roll back. We have said this before and we will keep saying it: kernel work is safe when you work in a safe place.

### Work Section by Section

The chapter is organised as a progression. Section 1 introduces VFS. Section 2 contrasts `devfs` with VFS and positions our driver in that contrast. Section 3 registers a minimal pseudo block device. Section 4 exposes it as a GEOM provider. Section 5 implements real read and write paths. Section 6 mounts a filesystem on top. Section 7 gives the device persistence. Section 8 teaches safe unmount and cleanup. Section 9 talks about refactoring, versioning, and what to do as the driver grows.

You are meant to read them in order. Each section assumes the previous sections are fresh in your mind, and the labs build on each other. If you jump in the middle, pieces will look strange.

### Type the Code

Typing the code by hand remains the most effective way to internalise kernel idioms. The companion files under `examples/part-06/ch27-storage-vfs/` exist so that you can check your work, not so that you can skip the typing. Reading code is not the same as writing it.

### Open the FreeBSD Source Tree

You will be asked several times to open real FreeBSD source files, not only the companion examples. The files of interest include `/usr/src/sys/geom/geom.h`, `/usr/src/sys/sys/bio.h`, `/usr/src/sys/geom/geom_disk.h`, `/usr/src/sys/dev/md/md.c`, and `/usr/src/sys/geom/zero/g_zero.c`. Each of these is a primary reference, and the prose in this chapter will often refer back to them. If you have not already cloned or installed the 14.3 source tree, now is a good moment to do so.

### Use Your Lab Logbook

Keep your lab logbook from Chapter 26 open while you work. You will want to record the output of `gstat -I 1`, the messages emitted by `dmesg` when you load and unload the module, the time it takes to format the device, and any warnings or panics you see. Kernel work is much easier when you keep notes, because many symptoms look similar at first glance and the logbook lets you compare across sessions.

### Pace Yourself

If you feel your understanding blurring in a particular section, stop. Read it again. Try a small experiment on the running module. Do not push through a section that has not settled. Storage drivers punish confusion more severely than character drivers, because confusion at the block layer often becomes filesystem corruption at the higher layer, and filesystem corruption takes time and care to repair even in a throwaway VM.

## How to Get the Most Out of This Chapter

The chapter is structured so that every section adds exactly one new concept on top of what came before. To make the most of that structure, treat the chapter as a workshop rather than as a reference. You are not here to find a quick answer. You are here to build a correct mental model.

### Work in Sections

Do not read the whole chapter end to end without stopping. Read one section, then pause. Try the experiment or lab that goes with it. Look at the related FreeBSD source. Write a few lines in your logbook. Only then move on. Storage programming in the kernel is strongly cumulative, and skipping ahead usually means that you will be confused about the next thing for a reason that was explained two sections ago.

### Keep the Driver Running

Once you have loaded the driver in Section 3, keep it loaded as much as possible while you read. Modify it, reload, poke it with `gstat`, run `dd` against it, call `diskinfo` on it. Having a live, observable example is far more valuable than any amount of reading. You will notice things that no chapter could ever tell you about, because no chapter can show you real timing, real jitter, or real corner cases in your particular setup.

### Consult Manual Pages

FreeBSD's manual pages are part of the teaching material, not a separate formality. Section 9 of the manual is where the kernel interfaces live. We will refer several times to pages such as `g_bio(9)`, `geom(4)`, `DEVICE_IDENTIFY(9)`, `disk(9)`, `bus_dma(9)`, and `devstat(9)`. Read them alongside this chapter. They are shorter than they look, and they are written by the same community that wrote the kernel you are working inside.

### Type the Code, Then Mutate It

When you build the driver from the companion examples, type it first. Once it works, start changing things. Rename a method and watch the build fail. Remove an `if` branch and watch what happens when you load the module. Hardcode a smaller media size and watch `newfs_ufs` react. Kernel code becomes understandable through deliberate mutation far more than through pure reading.

### Trust the Tooling

FreeBSD gives you a wealth of tools for inspecting the storage stack: `geom`, `gstat`, `diskinfo`, `dd`, `mdconfig`, `dmesg`, `kldstat`, `sysctl`. Use them. When something goes wrong, the first move is almost never to read more source. It is to ask the system what state it is in. `geom disk list` and `geom part show` are often more informative than five minutes of grep.

### Take Breaks

Kernel work is cognitively dense. Two or three focused hours are usually more productive than a seven-hour sprint. If you catch yourself making the same typo three times, or copy-pasting without reading, that is your cue to stand up for ten minutes.

With those habits established, let us begin.

## Section 1: What Is the Virtual File System Layer?

When a process opens a file on FreeBSD, it calls `open(2)` with a path. That path might resolve to a file on UFS, a file on ZFS, a file on a remotely mounted NFS share, a pseudo-file in `devfs`, a file under `procfs`, or even a file inside a FUSE-mounted userland filesystem. The process cannot tell. The process receives a file descriptor and then reads and writes as if there were only one kind of file in the world. That uniformity is not an accident. It is the work of the Virtual File System layer.

### The Problem VFS Solves

Before VFS, UNIX kernels generally knew how to talk to one filesystem only. If you wanted a new filesystem, you modified the code paths for `open`, `read`, `write`, `stat`, `unlink`, `rename`, and every other system call that touched files. That approach worked for a while, but it did not scale. New filesystems arrived: NFS, for remote access. MFS, for in-memory scratch space. Procfs, for exposing process state. ISO 9660, for CD-ROM media. FAT, for interoperability. Every addition meant new forks in every file-related system call.

Sun Microsystems introduced the Virtual File System architecture in the mid-1980s as a way out of this mess. The idea is simple. The kernel talks to a single abstract interface, defined in terms of generic operations on generic file objects. Each concrete filesystem registers implementations of those operations, and the kernel calls them through function pointers. When the kernel needs to read a file, it does not know or care whether the file lives on UFS or NFS or ZFS. It knows there is a node with a `VOP_READ` method, and it calls that method.

FreeBSD adopted this architecture and has extended it significantly over the decades. The result is that adding a filesystem to FreeBSD no longer requires modifying the core system calls. A filesystem is a separate kernel module that registers a set of operations with VFS, and from that moment VFS routes the right requests to it.

### The VFS Object Model

VFS defines three main kinds of objects.

The first is the **mount point**, represented in the kernel by `struct mount`. Every mounted filesystem has one, and it records where in the namespace the filesystem is attached, what flags it has, and which filesystem code is responsible for it.

The second is the **vnode**, represented by `struct vnode`. A vnode is the kernel's handle on a single file or directory within a mounted filesystem. It is not the file itself. It is the kernel's runtime representation of that file for as long as something in the kernel cares about it. Every file that a process has open has a vnode. Every directory the kernel is walking through has a vnode. When nothing holds a reference to a vnode, it can be reclaimed, and the kernel keeps a pool of them to avoid pressure on small-inode cases.

The third is the **vnode operations vector**, represented by `struct vop_vector`, which lists the operations each filesystem must implement on vnodes. The operations have names like `VOP_LOOKUP`, `VOP_READ`, `VOP_WRITE`, `VOP_CREATE`, `VOP_REMOVE`, `VOP_GETATTR`, and `VOP_SETATTR`. Each filesystem provides a pointer to its own vector, and the kernel invokes operations through these vectors whenever it needs to do anything to a file.

The elegant thing about this design is that from the system call side of the kernel, only the abstract interface matters. The system call layer calls `VOP_READ(vp, uio, ioflag, cred)` and does not care whether `vp` belongs to UFS, ZFS, NFS, or tmpfs. From the filesystem side, only the abstract interface matters too. UFS implements the vnode operations and never sees the system call code.

### Where Storage Drivers Fit

Here is the question that matters for this chapter. If VFS is where filesystems live, where do storage drivers live?

The answer is: not directly inside VFS. A storage driver does not implement `VOP_READ`. It implements a much lower-level abstraction that looks like a disk. Filesystems then sit on top, consuming the disk-like abstraction, translating file-level operations into block-level operations, and calling down.

The chain of layers between a process and a block device in FreeBSD typically looks like this.

```text
       +------------------+
       |   user process   |
       +--------+---------+
                |
                |  read(fd, buf, n)
                v
       +--------+---------+
       |   system calls   |  sys_read, sys_write, sys_open, ...
       +--------+---------+
                |
                v
       +--------+---------+
       |       VFS        |  vfs_read, VOP_READ, vnode cache
       +--------+---------+
                |
                v
       +--------+---------+
       |    filesystem    |  UFS, ZFS, NFS, tmpfs, ...
       +--------+---------+
                |
                v
       +--------+---------+
       |   buffer cache   |  bufcache, bwrite, bread, getblk
       +--------+---------+
                |
                |  struct bio
                v
       +--------+---------+
       |      GEOM        |  classes, providers, consumers
       +--------+---------+
                |
                v
       +--------+---------+
       |  storage driver  |  disk_strategy, bio handler
       +--------+---------+
                |
                v
       +--------+---------+
       |    hardware      |  real disk, SSD, or memory buffer
       +------------------+
```

Each layer in this stack has a job. VFS hides filesystem differences from system calls. The filesystem translates files into blocks. The buffer cache holds recently used blocks in RAM. GEOM routes block requests through transforms, partitions, and mirrors. The storage driver converts block requests into real I/O. The hardware does the work.

For this chapter, almost everything we do happens at the bottom two layers: GEOM and the storage driver. We will touch the filesystem layer briefly when we mount UFS on our device, and we will touch VFS only in the sense that `mount(8)` calls it. The layers above GEOM are not our code.

### VFS in the Kernel Source

If you want to look at VFS directly, the entry points are under `/usr/src/sys/kern/vfs_*.c`. The vnode layer lives in `vfs_vnops.c` and `vfs_subr.c`. The mount side lives in `vfs_mount.c`. The vnode operations vector is defined and handled in `vfs_default.c`. UFS, our primary filesystem in this chapter, lives under `/usr/src/sys/ufs/ufs/` and `/usr/src/sys/ufs/ffs/`. You do not need to read any of those to follow this chapter. You should know where they are so that you understand what sits above the code you are about to write.

### What This Means for Our Driver

Because VFS is not our direct caller, we do not need to implement `VOP_` methods. We need to implement the block-layer interface that the filesystem ultimately calls into. That interface is defined by GEOM and, for disk-like devices in particular, by the `g_disk` subsystem. Our driver will expose a GEOM provider. A filesystem will consume it. The flow of I/O will go through `struct bio` rather than through `struct uio`, and the unit of work will be a block rather than a byte range.

This is also why storage drivers rarely interact with `cdevsw` or `make_dev` directly the way character drivers do. The `/dev` node for a disk is created by GEOM, not by the driver. The driver describes itself to GEOM, and GEOM publishes a provider, which then appears in `/dev` with an automatically generated name.

### The VFS Call Chain in Practice

Let us trace what happens when a user runs `cat /mnt/myfs/hello.txt`, assuming `/mnt/myfs` is mounted on our future block device.

First, the process calls `open("/mnt/myfs/hello.txt", O_RDONLY)`. That goes to `sys_openat` in the system call layer, which asks VFS to resolve the path. VFS walks the path one component at a time, calling `VOP_LOOKUP` on each directory vnode. When it reaches `myfs`, it notices that the vnode is a mount point and crosses into the mounted filesystem. It eventually arrives at the vnode for `hello.txt` and returns a file descriptor.

Second, the process calls `read(fd, buf, 64)`. That goes to `sys_read`, which calls `vn_read`, which calls `VOP_READ` on the vnode. The UFS implementation of `VOP_READ` consults its inode, figures out which disk blocks hold the requested bytes, and asks the buffer cache for those blocks. If the blocks are not cached, the buffer cache calls `bread`, which ultimately builds a `struct bio` and hands it to GEOM.

Third, GEOM looks at the provider the filesystem is consuming. Through a chain of providers and consumers, the `bio` ends up at the bottom provider, which is our driver's provider. Our strategy function receives the `bio`, reads the requested bytes from our backing store, and calls `biodone` or `g_io_deliver` to complete the request.

Fourth, the reply travels back the other way. The buffer cache gets its data, the filesystem returns to `vn_read`, `vn_read` copies the data into the user buffer, and `sys_read` returns.

None of that code is ours except the last hop. But understanding the whole chain is what lets you make sensible design choices when you write the last hop.

### Wrapping Up Section 1

VFS is the layer that unifies filesystems in FreeBSD. It sits between the system call interface and the various concrete filesystems, and it provides the abstraction that makes files look identical regardless of where they live. Storage drivers do not live inside VFS. They live at the bottom of the stack, far below VFS, behind GEOM and the buffer cache. Our job in this chapter is to write a driver that participates correctly in that lower layer, and to understand enough about the upper layers to avoid confusion when diagnosing problems.

In the next section, we will sharpen the distinction between `devfs` and VFS, because that distinction determines which mental model applies when you think about a given device node.

## Section 2: devfs vs VFS

Beginners often assume that `devfs` and the Virtual File System layer are two names for the same thing. They are not. They are related, but they play very different roles. Getting this distinction right early saves a great deal of confusion later, especially when thinking about storage drivers, because storage drivers straddle both of them.

### What devfs Is

`devfs` is a filesystem. That sounds circular, but it is true. `devfs` is implemented as a filesystem module, registered with VFS, and mounted at `/dev` on every FreeBSD system. When you read a file under `/dev`, you are reading through VFS, which hands the request to `devfs`, which recognises that the "file" you are reading is really a kernel device node and routes the call to the appropriate driver.

`devfs` has several special properties that distinguish it from an ordinary filesystem like UFS.

First, its contents are not stored on disk. The "files" in `devfs` are synthesised by the kernel based on which drivers are currently loaded and which devices are currently present. When a driver calls `make_dev(9)` to create `/dev/mybox`, `devfs` adds the corresponding node to its view. When the driver destroys that device with `destroy_dev(9)`, `devfs` removes the node. The user sees `/dev/mybox` appear and disappear in real time.

Second, the read and write paths for `devfs` nodes are not file data paths. When you write to `/dev/myserial0`, you are not appending bytes to a stored file. You are invoking the driver's `d_write` function through `cdevsw`, and that function decides what those bytes mean. In the case of a USB serial driver, they mean bytes to transmit on the wire. In the case of a pseudo device like `/dev/null`, they mean bytes to discard.

Third, the metadata of `devfs` nodes, such as permissions and ownership, is managed by a policy layer in the kernel rather than by the filesystem itself. `devfs_ruleset(8)` and the `devd` framework configure that policy.

Fourth, `devfs` supports cloning, which character drivers like `pty`, `tun`, and `bpf` use to create a new minor device whenever a process opens the node. This is how `/dev/ptyp0`, `/dev/ptyp1`, and their successors come into existence on demand.

### What VFS Is

VFS, as we saw in Section 1, is the abstract filesystem layer. Every filesystem on a FreeBSD system, including `devfs`, is registered with VFS and invoked through VFS. VFS is not a filesystem. It is the framework that filesystems plug into.

When you open a file on UFS, the chain is: system call -> VFS -> UFS -> buffer cache -> GEOM -> driver. When you open a node in `devfs`, the chain is: system call -> VFS -> devfs -> driver. Both go through VFS. Only the UFS chain involves GEOM.

### Why Storage Drivers Live on Both Sides

This is where storage drivers become interesting.

A storage driver exposes a block device, and that block device eventually appears as a node under `/dev`. For example, if we register our driver and tell GEOM about it, a node called `/dev/myblk0` may appear in `devfs`. When a user writes `dd if=image.iso of=/dev/myblk0`, they are writing through `devfs` to a special character interface that GEOM provides on top of our disk. The requests flow as BIO through GEOM and into our strategy function.

But when a user runs `newfs_ufs /dev/myblk0` and then `mount /dev/myblk0 /mnt`, the usage pattern changes. The kernel now mounts UFS on top of the device. When a process later reads a file under `/mnt`, the path is: system call -> VFS -> UFS -> buffer cache -> GEOM -> driver. The `/dev/myblk0` node in `devfs` is not even involved in the hot path. UFS and the buffer cache talk directly to the GEOM provider. The `devfs` node is essentially a handle that tools use to refer to the device, not the pipe that file data flows through during normal operation.

### A Closer Look at the Buffer Cache

Between the filesystem and GEOM in the storage path sits the buffer cache. We have mentioned it several times without pausing to describe it. Let us pause now, because it explains several of the behaviours you will observe when testing your driver.

The buffer cache is a pool of fixed-size buffers in kernel memory, each of which holds one filesystem block. When a filesystem reads a block, the buffer cache gets involved: the filesystem asks the cache for the block, and the cache either returns a hit (the block is already in memory) or issues a miss (the cache allocates a buffer, calls down through GEOM to fetch the data, and returns the buffer once the read completes). When a filesystem writes a block, the same cache path applies in reverse: the write fills a buffer, the buffer is marked dirty, and the cache schedules a write-back at some later point.

The buffer cache is why consecutive reads of the same file data do not always hit the driver. The first read misses, causing a BIO to travel to the driver. The second read hits the cache and returns immediately. This is a great feature for performance. It can be mildly confusing when you are first debugging a driver, because your `printf` in the strategy function does not fire on every user-space read.

The buffer cache is also why writes can appear to happen faster than the underlying driver. A `dd if=/dev/zero of=/mnt/myblk/big bs=1m count=16` may appear to complete in a fraction of a second because the writes land in the cache and the cache defers the actual BIOs for a while. The filesystem issues the real writes to GEOM over the next second or two. If the system crashes before that happens, the file on disk is incomplete. `sync(2)` forces the cache to flush to the underlying device. `fsync(2)` flushes only the buffers associated with a single file descriptor.

The buffer cache is distinct from the page cache. FreeBSD has both, and they cooperate. The page cache holds memory pages that back memory-mapped files and anonymous memory. The buffer cache holds buffers that back filesystem-block operations. Modern FreeBSD has largely unified them for many data paths, but the distinction still shows up in the source tree, particularly around `bread`, `bwrite`, `getblk`, and `brelse`, which are the buffer-cache side of the interface.

The buffer cache has a single most important implication for our driver: we will almost never see completely synchronous BIO traffic. When a filesystem wants to read a block, a BIO arrives in our strategy function; when a filesystem wants to write a block, another BIO arrives, but usually some time later than the write system call that prompted it. BIOs also arrive in bursts when the cache flushes. This is normal, and your driver must not make assumptions about timing or ordering across BIOs other than what is strictly documented. Each BIO is an independent request.

### The Read and Write Paths

Let us trace a concrete example through the whole chain.

When a user runs `cat /mnt/myblk/hello.txt`, the shell runs `cat`, which calls `open("/mnt/myblk/hello.txt", O_RDONLY)`. The `open` goes to `sys_openat`, which hands off to VFS. VFS calls `namei` to walk the path. For each path component, VFS calls `VOP_LOOKUP` on the current directory's vnode. When VFS reaches the `myblk` mount, it crosses into UFS, which walks the UFS directory structure to find `hello.txt`. UFS returns the vnode for that file, and VFS returns a file descriptor.

The user then calls `read(fd, buf, 64)`. `sys_read` calls `vn_read`, which calls `VOP_READ` on the vnode. UFS's `VOP_READ` consults the inode to find the block address of the requested bytes, then calls `bread` on the buffer cache to fetch the block. The buffer cache either returns a hit or issues a BIO.

If it is a cache miss, the buffer cache allocates a fresh buffer, builds a BIO that asks for the relevant block from the underlying GEOM provider, and hands it off. The BIO travels down through GEOM, through our strategy function, and back. When the BIO completes, the buffer cache unblocks the waiting `bread` call. UFS then copies the requested bytes from the buffer into the user's `buf`. `read` returns.

For writes, the chain is symmetric but the timing is different. UFS's `VOP_WRITE` calls `bread` or `getblk` to obtain the target buffer, copies the user's data into the buffer, marks the buffer dirty, and calls `bdwrite` or `bawrite` to schedule the write-back. The user's `write` call returns long before the BIO is issued to the driver. Later, the buffer cache's syncer thread picks up dirty buffers and issues BIO_WRITE requests to the driver.

The net effect is that our driver's strategy function sees a stream of BIOs that is related to, but not identical to, the stream of user-space reads and writes. The buffer cache mediates the two.

In other words, the same storage driver can be reached two different ways.

1. **Raw access through `/dev`**: a user-space program opens `/dev/myblk0` and issues `read(2)` or `write(2)` calls. Those calls go through `devfs` and the GEOM character interface, ending up in our strategy function.
2. **Filesystem access through mount**: the kernel mounts a filesystem on the device. File I/O flows through VFS, the filesystem, the buffer cache, and GEOM. `devfs` is not part of the hot path for those requests.

Both paths converge at the GEOM provider, which is why GEOM is the correct abstraction for storage drivers even though character drivers typically deal with `devfs` more directly.

### Why This Distinction Matters

This matters for two reasons.

First, it clarifies why we will not use `make_dev` for our block driver. `make_dev` is the right call for character drivers that want to publish a `cdevsw` under `/dev`. It is the wrong call for a block device, because GEOM creates the `/dev` node for us as soon as we publish a provider. If you call `make_dev` in a storage driver, you typically end up with two `/dev` nodes competing for the same device, one of which is not connected to the GEOM topology, which leads to confusing behaviour.

Second, the distinction explains why the kernel has two sets of tools for inspecting device state. `devfs_ruleset(8)`, `devfs.rules`, and per-node permissions belong to `devfs`. `geom(8)`, `gstat(8)`, `diskinfo(8)`, and the GEOM class tree belong to GEOM. When you are diagnosing a permissions problem, you look at `devfs`. When you are diagnosing an I/O problem, you look at GEOM.

### A Concrete Example: /dev/null and /dev/ada0

Compare two examples you already know.

`/dev/null` is a classic character device. It lives under `/dev` because `devfs` creates it. The driver is `null(4)`, and its source is in `/usr/src/sys/dev/null/null.c`. When you write to `/dev/null`, `devfs` routes the request through `cdevsw` to the `null` driver's write function, which simply discards the bytes. There is no GEOM, no buffer cache, no filesystem. It is a raw `devfs` character node.

`/dev/ada0` is a block device. It also lives under `/dev`. But the node is created by GEOM, not by a direct `make_dev` call in the `ada` driver. When you read raw bytes from `/dev/ada0`, those bytes flow through GEOM's character interface layer and arrive at the `ada` driver's strategy function. When you mount UFS on `/dev/ada0` and then read a file, the file data flows through VFS, UFS, the buffer cache, and GEOM, and ends up in the same strategy function, without passing through `devfs` for each request.

The node in `devfs` is the same. The usage pattern is different. The driver must handle both.

### How We Will Proceed

We will not write a character driver in this chapter. We already wrote one in Chapter 26. Instead, we will write a driver that registers with GEOM as a disk, and we will let GEOM create the `/dev` node for us. The devfs integration will be automatic.

This is the dominant pattern for block drivers in FreeBSD 14.3. You can see it in `md(4)`, in `ata(4)`, in `nvme(4)`, and in almost every other storage driver. Each of them registers with GEOM, each of them receives `bio` requests, and each of them lets GEOM handle the `/dev` node.

### Wrapping Up Section 2

`devfs` and VFS are distinct layers. `devfs` is a filesystem mounted at `/dev`, and VFS is the abstract framework that all filesystems plug into, including `devfs`. Storage drivers interact with both, but through GEOM, which takes care of creating the `/dev` node and of routing requests from both raw-access and filesystem-access paths. For this chapter, we will use GEOM as our entry point and let it manage `devfs` on our behalf.

In the next section, we will begin building the driver. We will start with the minimum needed to register a pseudo block device with GEOM, without yet implementing real I/O. Once that is in place, we will add the backing store, the `bio` handler, and everything else in later sections.

## Section 3: Registering a Pseudo Block Device

In this section we will create a skeleton driver that registers a pseudo block device with the kernel. We will not yet implement read or write. We will not yet wire it up to a backing store. Our goal is more modest and more important: we want to understand exactly what it takes to make the kernel recognise our code as a storage driver, publish a `/dev` node for it, and let tools like `geom(8)` see it.

Once that is working, everything we add later will be purely incremental. The registration itself is the step that feels most mysterious, and it is the one the rest of the driver builds on.

### The g_disk API

FreeBSD gives storage drivers a high-level registration API called `g_disk`. It lives in `/usr/src/sys/geom/geom_disk.c` and `/usr/src/sys/geom/geom_disk.h`. The API wraps the lower-level GEOM class machinery and exposes a simpler interface that matches what disk drivers usually need.

Using `g_disk` saves us from implementing a full `g_class` by hand. With `g_disk`, we allocate a `struct disk`, fill in a handful of fields and callback pointers, and call `disk_create`. The API takes care of building the GEOM class, creating the geom, publishing the provider, wiring up the character interface, starting devstat accounting, and making our device visible to userland through `/dev`.

Not every storage driver uses `g_disk`. GEOM classes that do transformations on other providers, like `g_nop`, `g_mirror`, `g_stripe`, or `g_eli`, are built directly on the lower-level `g_class` machinery because they are not disk-shaped. But for anything that looks like a disk, and certainly for a pseudo-disk like ours, `g_disk` is the right starting point.

You can see the public structure in `/usr/src/sys/geom/geom_disk.h`. The shape is roughly the following, abbreviated for clarity.

```c
struct disk {
    struct g_geom    *d_geom;
    struct devstat   *d_devstat;

    const char       *d_name;
    u_int            d_unit;

    disk_open_t      *d_open;
    disk_close_t     *d_close;
    disk_strategy_t  *d_strategy;
    disk_ioctl_t     *d_ioctl;
    disk_getattr_t   *d_getattr;
    disk_gone_t      *d_gone;

    u_int            d_sectorsize;
    off_t            d_mediasize;
    u_int            d_fwsectors;
    u_int            d_fwheads;
    u_int            d_maxsize;
    u_int            d_flags;

    void             *d_drv1;

    /* other fields elided */
};
```

The fields break down into three groups.

**Identification**: `d_name` is a short string like `"myblk"` that names the disk class, and `d_unit` is a small integer that distinguishes multiple instances. Together they form the `/dev` node name. A driver with `d_name = "myblk"` and `d_unit = 0` publishes `/dev/myblk0`.

**Callbacks**: the `d_open`, `d_close`, `d_strategy`, `d_ioctl`, `d_getattr`, and `d_gone` pointers are the functions the kernel will call into our driver. Of these, only `d_strategy` is strictly required, because that is the function that handles actual I/O. The others are optional and we will discuss them as they become relevant.

**Geometry**: `d_sectorsize`, `d_mediasize`, `d_fwsectors`, `d_fwheads`, and `d_maxsize` describe the disk's physical and logical shape. `d_sectorsize` is the size of a sector in bytes, typically 512 or 4096. `d_mediasize` is the total size of the device in bytes. `d_fwsectors` and `d_fwheads` are advisory hints used by partitioning tools. `d_maxsize` is the largest single I/O the driver can accept, which GEOM will use to split large requests.

**Driver state**: `d_drv1` is a generic pointer for the driver to stash its own context. It is the closest equivalent to `device_get_softc(dev)` in the Newbus world.

### A Minimum Skeleton

Let us now sketch a minimum skeleton. We will place this in `examples/part-06/ch27-storage-vfs/myfirst_blk.c`. This initial version does almost nothing useful. It registers a disk, returns success on every operation, and unregisters cleanly on unload. But it is enough to appear in `/dev`, to be visible in `geom disk list`, and to be probed by `newfs_ufs` or `fdisk` without the kernel crashing.

```c
/*
 * myfirst_blk.c - a minimal pseudo block device driver.
 *
 * This driver registers a single pseudo disk called myblk0 with
 * the g_disk subsystem. It is intentionally not yet capable of
 * performing real I/O. Sections 4 and 5 of Chapter 27 will add
 * the BIO handler and the backing store.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/bio.h>

#include <geom/geom.h>
#include <geom/geom_disk.h>

#define MYBLK_NAME       "myblk"
#define MYBLK_SECTOR     512
#define MYBLK_MEDIASIZE  (1024 * 1024)   /* 1 MiB to start */

struct myblk_softc {
    struct disk     *disk;
    struct mtx       lock;
    u_int            unit;
};

static MALLOC_DEFINE(M_MYBLK, "myblk", "myfirst_blk driver state");

static struct myblk_softc *myblk_unit0;

static void
myblk_strategy(struct bio *bp)
{

    /*
     * No real I/O yet. Mark every request as successful
     * but unimplemented so the caller does not hang.
     */
    bp->bio_error = ENXIO;
    bp->bio_flags |= BIO_ERROR;
    bp->bio_resid = bp->bio_bcount;
    biodone(bp);
}

static int
myblk_attach_unit(struct myblk_softc *sc)
{

    sc->disk = disk_alloc();
    sc->disk->d_name       = MYBLK_NAME;
    sc->disk->d_unit       = sc->unit;
    sc->disk->d_strategy   = myblk_strategy;
    sc->disk->d_sectorsize = MYBLK_SECTOR;
    sc->disk->d_mediasize  = MYBLK_MEDIASIZE;
    sc->disk->d_maxsize    = MAXPHYS;
    sc->disk->d_drv1       = sc;

    disk_create(sc->disk, DISK_VERSION);
    return (0);
}

static void
myblk_detach_unit(struct myblk_softc *sc)
{

    if (sc->disk != NULL) {
        disk_destroy(sc->disk);
        sc->disk = NULL;
    }
}

static int
myblk_loader(struct module *m, int what, void *arg)
{
    struct myblk_softc *sc;
    int error;

    switch (what) {
    case MOD_LOAD:
        sc = malloc(sizeof(*sc), M_MYBLK, M_WAITOK | M_ZERO);
        mtx_init(&sc->lock, "myblk lock", NULL, MTX_DEF);
        sc->unit = 0;
        error = myblk_attach_unit(sc);
        if (error != 0) {
            mtx_destroy(&sc->lock);
            free(sc, M_MYBLK);
            return (error);
        }
        myblk_unit0 = sc;
        printf("myblk: loaded, /dev/%s%u size=%jd bytes\n",
            MYBLK_NAME, sc->unit,
            (intmax_t)sc->disk->d_mediasize);
        return (0);

    case MOD_UNLOAD:
        sc = myblk_unit0;
        if (sc == NULL)
            return (0);
        myblk_detach_unit(sc);
        mtx_destroy(&sc->lock);
        free(sc, M_MYBLK);
        myblk_unit0 = NULL;
        printf("myblk: unloaded\n");
        return (0);

    default:
        return (EOPNOTSUPP);
    }
}

static moduledata_t myblk_mod = {
    "myblk",
    myblk_loader,
    NULL
};

DECLARE_MODULE(myblk, myblk_mod, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);
MODULE_VERSION(myblk, 1);
```

Take a moment to read this through. Only a handful of moving pieces are visible, but each of them is doing real work.

The `myblk_softc` structure is the driver-local context. It holds a pointer to our `struct disk`, a mutex for future use, and the unit number. We allocate it on module load and free it on unload.

The `myblk_strategy` function is the callback that GEOM will invoke whenever a `bio` is directed at our device. In this first version, we simply fail every request with `ENXIO`. That is not polite, but it is correct as a placeholder: the kernel will not block waiting for us, and we will not pretend that I/O succeeded when it did not. In Section 5 we will replace this with a working handler.

The `myblk_attach_unit` function allocates a `struct disk`, fills in the identification, callbacks, and geometry fields, and publishes it with `disk_create`. The call to `disk_create` is what actually produces the `/dev` node and registers the disk in the GEOM topology.

The `myblk_detach_unit` function reverses that. `disk_destroy` asks GEOM to wither the provider, cancel any pending I/O, and remove the `/dev` node. We set `sc->disk` to `NULL` so that subsequent unload attempts do not try to free an already-freed structure, though in the load/unload path we follow that cannot happen.

The module loader is a standard `moduledata_t` boilerplate that you saw in Chapter 26. On `MOD_LOAD` it allocates the softc and calls `myblk_attach_unit`. On `MOD_UNLOAD` it calls `myblk_detach_unit`, frees the softc, and returns.

One line deserves special attention.

The call `disk_create(sc->disk, DISK_VERSION)` passes the current ABI version of the disk structure. `DISK_VERSION` is defined in `/usr/src/sys/geom/geom_disk.h` and increments every time the `g_disk` ABI changes incompatibly. If you compile a driver against the wrong tree, the kernel will refuse to register the disk and will print a diagnostic. This versioning is what allows the kernel to evolve without silently breaking out-of-tree drivers.

You may wonder why we do not use `MODULE_DEPEND` to declare a dependency on `g_disk`. The reason is that `g_disk` is not a loadable kernel module in the usual sense. It is a GEOM class declared in the kernel via `DECLARE_GEOM_CLASS(g_disk_class, g_disk)` in `/usr/src/sys/geom/geom_disk.c`, and it is always present whenever GEOM itself is compiled into the kernel. There is no separate `g_disk.ko` file you can unload or reload independently, and `MODULE_DEPEND(myblk, g_disk, ...)` would not resolve to a real module. The symbols we call (`disk_alloc`, `disk_create`, `disk_destroy`) come from the kernel itself.

### The Makefile

The Makefile for this module is almost identical to the one from Chapter 26.

```make
# Makefile for myfirst_blk.
#
# Companion file for Chapter 27 of
# "FreeBSD Device Drivers: From First Steps to Kernel Mastery".

KMOD    = myblk
SRCS    = myfirst_blk.c

# Where the kernel build machinery lives.
.include <bsd.kmod.mk>
```

Place this in the same directory as `myfirst_blk.c`. Running `make` will build `myblk.ko`. Running `make load` will load it if you have the kernel sources installed in the usual place. Running `make unload` will unload it.

### Loading and Inspecting the Skeleton

Once the module is loaded, the kernel will have created a pseudo disk and a `/dev` node for it. Let us walk through what you should see.

```console
# kldload ./myblk.ko
# dmesg | tail -n 1
myblk: loaded, /dev/myblk0 size=1048576 bytes
# ls -l /dev/myblk0
crw-r-----  1 root  operator  0x8b Apr 19 18:04 /dev/myblk0
# diskinfo -v /dev/myblk0
/dev/myblk0
        512             # sectorsize
        1048576         # mediasize in bytes (1.0M)
        2048            # mediasize in sectors
        0               # stripesize
        0               # stripeoffset
        myblk0          # Disk ident.
```

The `c` at the beginning of the permissions string tells us that GEOM has created a character device node, which is how FreeBSD exposes block-oriented devices under `/dev` in the modern kernel. The device major number, here `0x8b`, is assigned dynamically.

Now let us look at the GEOM topology.

```console
# geom disk list myblk0
Geom name: myblk0
Providers:
1. Name: myblk0
   Mediasize: 1048576 (1.0M)
   Sectorsize: 512
   Mode: r0w0e0
   descr: (null)
   ident: (null)
   rotationrate: unknown
   fwsectors: 0
   fwheads: 0
```

`Mode: r0w0e0` means zero readers, zero writers, zero exclusive holders. Nobody is using the disk.

Now try something harmless.

```console
# dd if=/dev/myblk0 of=/dev/null bs=512 count=1
dd: /dev/myblk0: Device not configured
0+0 records in
0+0 records out
0 bytes transferred in 0.000123 secs (0 bytes/sec)
```

The `Device not configured` error is the `ENXIO` we deliberately returned. Our strategy function ran, marked the BIO as failed, and `dd` faithfully reported the failure. This is the first real evidence that our driver is being reached by the kernel's block-layer code.

Try a read that expects success to fail loudly.

```console
# newfs_ufs /dev/myblk0
newfs: /dev/myblk0: read-only
# newfs_ufs -N /dev/myblk0
/dev/myblk0: 1.0MB (2048 sectors) block size 32768, fragment size 4096
        using 4 cylinder groups of 0.31MB, 10 blks, 40 inodes.
super-block backups (for fsck_ffs -b #) at:
192, 832, 1472, 2112
```

The `-N` flag tells `newfs` to plan the filesystem layout without writing anything. We can see that it thinks of our device as a small disk with 2048 sectors of 512 bytes each. That matches the geometry we declared. It is not yet actually writing anything because our strategy function would still fail, but the planning works.

Finally, let us unload the module cleanly.

```console
# kldunload myblk
# dmesg | tail -n 1
myblk: unloaded
# ls /dev/myblk0
ls: /dev/myblk0: No such file or directory
```

That is the complete life cycle of the skeleton.

### Why the Failures Are Expected

At this stage, any user-space tool that actually tries to read or write data will fail. That is correct. Our strategy function does not yet know how to do anything, and we must not fake success. Faking success would lead to corruption the moment a filesystem tried to read back what it thought it had written.

The fact that the kernel and the tools gracefully handle our failure is evidence that the block layer is doing its job. A `bio` came down, the driver rejected it, the error propagated back up to user space, and no one crashed. That is the kind of behaviour we want.

### How Things Fit Together

Before moving on, let us name the pieces so we can refer to them later without ambiguity.

Our **driver module** is `myblk.ko`. It is what the user loads with `kldload`.

Our **softc** is `struct myblk_softc`. It holds driver-local state. There is exactly one instance in this first version.

Our **disk** is a `struct disk` allocated by `disk_alloc` and registered with `disk_create`. The kernel owns its memory. We do not free it directly. We ask the kernel to free it by calling `disk_destroy`.

Our **geom** is the GEOM object the `g_disk` subsystem creates on our behalf. We do not see it directly in our code. It exists in the GEOM topology as the parent of our provider.

Our **provider** is the producer-facing face of our device. It is what other GEOM classes consume when they connect to us. GEOM automatically creates a character device node for our provider under `/dev`.

Our **consumer** is still empty. We have no one connected to us yet. Consumers are how GEOM classes that sit above us, like a partitioning layer or a filesystem's GEOM consumer, attach.

Our **/dev node** is `/dev/myblk0`. It is a live handle that user-space tools can use to issue raw I/O. When a filesystem is later mounted on the device, it will also refer to the device by this name, even though the hot I/O path will not pass through `devfs` for each request.

### Wrapping Up Section 3

We built the smallest possible driver that participates in the FreeBSD storage stack. It registers a pseudo disk with the `g_disk` subsystem, publishes a `/dev` node through GEOM, accepts BIO requests, and declines them politely. It loads, it appears in `geom disk list`, and it unloads without leaks.

In the next section, we will look at GEOM more directly. We will understand what a provider really is, what a consumer really is, and how the class-based design lets transformations like partitioning, mirroring, encryption, and compression compose with our driver for free. That understanding will set us up for Section 5, where we replace the placeholder strategy function with one that actually serves reads and writes from a backing store.

## Section 4: Exposing a GEOM-Backed Provider

The previous section let us register a disk with `g_disk` and take the word of the framework for what happens under the hood. That is a reasonable first step, and for many drivers it is all the involvement with GEOM that they ever need. But storage work rewards understanding the layer you are sitting on. When a filesystem mount fails, when `gstat` shows requests piling up, or when a `kldunload` blocks for longer than you expect, you will want to know the vocabulary of GEOM well enough to ask the right questions.

This section is a tour of GEOM from the storage-driver perspective. It is not an exhaustive reference. There are entire chapters in the FreeBSD Developer's Handbook devoted to GEOM, and we will not duplicate them. What we will do is describe the concepts and objects that matter to a driver author, and show how `g_disk` fits into that picture.

### GEOM in One Page

GEOM is a storage framework. It sits between filesystems and the block drivers that talk to real hardware, and it composes by design. That composition is the whole point.

The idea is that a storage stack is built out of small transformations. One transformation presents a raw disk. Another transformation splits it into partitions. Another transformation mirrors two disks into one. Another transformation encrypts a partition. Another transformation compresses a filesystem. Each transformation is a small piece of code that takes in I/O requests from above, does something to them, and either returns a result directly or passes them along to the next layer below.

In GEOM's vocabulary, each transformation is a **class**. Each instance of a class is a **geom**. Each geom has some number of **providers**, which are its outputs, and some number of **consumers**, which are its inputs. Providers face upward toward the next layer. Consumers face downward toward the previous layer. A geom with no consumer is at the bottom of the stack: it must produce I/O on its own. A geom with no provider is at the top of the stack: it must terminate I/O and deliver it somewhere outside GEOM, typically into a filesystem or into a `devfs` character device.

Requests flow from providers to consumers through the stack. Replies flow back. The unit of I/O is a `struct bio`, which we will study in detail in Section 5.

### A Concrete Example of Composition

Imagine you have a 1 TB SATA SSD. The kernel's `ada(4)` driver runs on the SATA controller and publishes a disk provider called `ada0`. That is a geom with no consumer at the bottom and one provider at the top.

You slice the SSD with `gpart`. The `PART` class creates a geom whose single consumer is attached to `ada0`, and which publishes multiple providers, one per partition: `ada0p1`, `ada0p2`, `ada0p3`, and so on.

You encrypt `ada0p2` with `geli`. The `ELI` class creates a geom whose single consumer is attached to `ada0p2`, and which publishes a single provider called `ada0p2.eli`.

You mount UFS on `ada0p2.eli`. UFS opens that provider, reads its superblock, and begins serving files.

When a process reads a file, the request travels from UFS, to `ada0p2.eli`, through the `geli` geom which decrypts the relevant blocks, to `ada0p2`, through the `PART` geom which offsets the block addresses, to `ada0`, where the `ada` driver talks to the SATA controller.

At no point does UFS know that its underlying storage is encrypted, partitioned, or even a physical disk. It just sees a provider. The layers below it can be as simple or as elaborate as the administrator chooses.

That composition is the reason GEOM exists. A single storage driver only needs to know how to be a reliable bottom-of-stack producer of I/O. Everything above it is reusable.

### Providers and Consumers in Code

In the kernel, a provider is a `struct g_provider` and a consumer is a `struct g_consumer`. Both are defined in `/usr/src/sys/geom/geom.h`. As a disk driver author, you almost never allocate either directly. `g_disk` allocates a provider on your behalf when you call `disk_create`, and you never need a consumer, because a disk driver does not attach to anything underneath.

What you do need is a mental model of what they mean.

A provider is a named, seekable, block-addressable surface that something can read and write. It has a size, a sector size, a name, and some access counters. GEOM publishes providers in `/dev` via its character-device integration, so the administrator can refer to them by name.

A consumer is a channel from one geom into another geom's provider. The consumer is where the upper geom issues I/O requests, and it is where the upper geom registers access rights. When you mount UFS on `ada0p2.eli`, the mount operation causes a consumer to be attached inside UFS's GEOM hook, and that consumer acquires access rights on the `ada0p2.eli` provider.

### Access Rights

Providers have three access counters: read (`r`), write (`w`), and exclusive (`e`). They are visible in `gstat` and `geom disk list` as `r0w0e0` or similar. Each number is incremented when a consumer asks for that kind of access and decremented when the consumer releases it.

An exclusive access is what `mount`, `newfs`, and similar administrative tools acquire when they need to be sure no other process is writing the device. An exclusive count of zero means no exclusive access is held. An exclusive count greater than zero means the provider is busy.

The access counts are not trivia. They are a real synchronisation tool. When you call `disk_destroy` to remove a disk, the kernel will refuse to destroy the provider if it still has open users, because destroying it under the feet of a mounted filesystem would be catastrophic. This is the same mechanism that makes `kldunload` block if the module is in use, but it operates at the GEOM layer, one level higher than the module subsystem.

You can watch the access counters change in real time.

```console
# geom disk list myblk0 | grep Mode
   Mode: r0w0e0
# dd if=/dev/myblk0 of=/dev/null bs=512 count=1 &
# geom disk list myblk0 | grep Mode
   Mode: r1w0e0
```

When `dd` finishes, the mode returns to `r0w0e0`.

### The BIO Object and Its Life Cycle

The unit of work in GEOM is the BIO, defined as `struct bio` in `/usr/src/sys/sys/bio.h`. A BIO represents one I/O request. It has a command (`bio_cmd`), an offset (`bio_offset`), a length (`bio_length`), a data pointer (`bio_data`), a byte count (`bio_bcount`), a residual (`bio_resid`), an error (`bio_error`), flags (`bio_flags`), and a number of other fields that we will meet as we need them.

The `bio_cmd` values tell the driver what kind of I/O is being requested. The most common values are `BIO_READ`, `BIO_WRITE`, `BIO_DELETE`, `BIO_GETATTR`, and `BIO_FLUSH`. `BIO_READ` and `BIO_WRITE` are what you expect. `BIO_DELETE` asks the driver to release the blocks in the range, the way `TRIM` does on SSDs or `mdconfig -d` does on a memory disk. `BIO_GETATTR` queries an attribute by name and is how GEOM layers discover partition types, media labels, and other metadata. `BIO_FLUSH` asks the driver to commit outstanding writes to stable storage.

A BIO travels downward from one geom to the next via `g_io_request`. When it reaches the bottom of the stack, the driver's strategy function is called. When the driver is done, it completes the BIO by calling `biodone` or, at the GEOM class level, `g_io_deliver`. The completion call releases the BIO back up the stack.

`g_disk` drivers get a slightly simpler view because the `g_disk` infrastructure translates GEOM-level BIO handling into `biodone`-style completion. When you implement `d_strategy`, you receive a `struct bio` and you must eventually call `biodone(bp)` to complete it. You do not call `g_io_deliver` directly. The framework does.

### The GEOM Topology Lock

GEOM has a global lock called the topology lock. It protects modifications to the tree of geoms, providers, and consumers. When a provider is created or destroyed, when a consumer is attached or detached, when access counts change, or when GEOM walks the tree to route a request, the topology lock is taken.

The topology lock is held across operations that can take time, which is unusual for kernel locks, so GEOM operates much of its real work asynchronously through a dedicated thread called the event queue. When you look at `g_class` definitions in the source tree, the `init`, `fini`, `access`, and similar methods are invoked in the context of the GEOM event thread, not in the context of the user process that triggered the operation.

For a driver using `g_disk`, this matters in one specific way. You should not hold your own driver lock across a call into GEOM-level functions, because GEOM may acquire the topology lock inside those functions, and nested locking in the wrong order leads to deadlock. `g_disk` is written carefully enough that you do not usually have to think about this as long as you follow the patterns we show. But the fact is worth knowing.

### The GEOM Event Queue

GEOM processes many events on a single dedicated kernel thread called `g_event`. If you have the kernel running with debugging enabled, you can see it in `procstat -kk`. This thread picks up events placed on its queue and processes them one at a time. Typical events include creating a geom, destroying a geom, attaching a consumer, detaching a consumer, and retasting a provider.

A practical consequence is that some actions you take from your driver, such as `disk_destroy`, do not happen synchronously in the context of the calling thread. They get queued for the event thread, and the actual destruction happens a moment later. `disk_destroy` handles the waiting correctly so that by the time it returns, the disk is gone. But if you are chasing a subtle ordering bug, remembering that GEOM has its own thread can help.

### How g_disk Wraps All of This

With that vocabulary in hand, we can now describe what `g_disk` does for us more precisely.

When we call `disk_alloc`, we receive a `struct disk` that is pre-initialised enough to be filled in. We set the name, unit, callbacks, and geometry, then call `disk_create`.

`disk_create` does the following for us, through the event queue:

1. creates a GEOM class if one does not already exist for this disk name,
2. creates a geom under that class,
3. creates a provider associated with the geom,
4. sets up devstat accounting so that `iostat` and `gstat` have data,
5. wires up GEOM's character-device interface so that `/dev/<name><unit>` appears,
6. arranges for BIO requests to flow into our `d_strategy` callback.

It also sets up a few optional behaviours. If we provide a `d_ioctl`, the kernel routes user-space `ioctl` calls on the `/dev` node through to our function. If we provide a `d_getattr`, GEOM routes `BIO_GETATTR` requests through to it. If we provide a `d_gone`, the kernel calls it if something outside our driver decides the disk is gone, such as a hotplug removal event.

On the teardown side, `disk_destroy` queues the removal, waits for all pending I/O to drain, releases the provider, destroys the geom, and frees the `struct disk`. We do not call `free` on the disk ourselves. The framework does that.

### Where to Read Source

You now have enough vocabulary to benefit from reading the `g_disk` source directly. Open `/usr/src/sys/geom/geom_disk.c` and look for the following.

The function `disk_alloc` is early in the file. It is a simple allocator that returns a zeroed `struct disk`. Nothing dramatic.

The function `disk_create` is longer. Skim it and notice the event-based approach: most of the real work is queued rather than performed inline. Also notice the sanity checks on the disk's fields, which catch drivers that forget to set the sector size, the media size, or the strategy function.

The function `disk_destroy` is similarly event-queued. It guards the teardown with an access-count check, because destroying a disk that is still open would be a bug.

The function `g_disk_start` is the inner strategy function. It validates a BIO, updates devstat, and calls the driver's `d_strategy`.

Take a moment to look at the code. You do not need to understand every branch. You do need to recognise the overall shape: events for structural changes, inline work for I/O. That is the shape of most GEOM-based code.

### Comparing md(4) and g_zero

Two real drivers make good reading as counterpoints to `g_disk`. The first is the `md(4)` driver, in `/usr/src/sys/dev/md/md.c`. This is a memory-disk driver that uses both `g_disk` and directly managed GEOM structures. It is the most thorough example of a storage driver in the tree, supporting multiple backing-store types, resizing, dumping, and many other features. It is a large file, but it is the closest relative of what we are building.

The second is `g_zero`, in `/usr/src/sys/geom/zero/g_zero.c`. This is a minimal GEOM class that reads always return zeroed memory and writes are discarded. It is roughly 145 lines and uses the lower-level `DECLARE_GEOM_CLASS` API directly rather than `g_disk`. It is a great counterpoint because it shows the GEOM class mechanics without any of the disk-specific adornment. When you want to understand what `g_disk` hides, read `g_zero`.

### Why Our Driver Uses g_disk

You might ask whether we should build our driver directly on the lower-level `g_class` API, the way `g_zero` does, to expose more of the machinery. We will not, for three reasons.

First, `g_disk` is the idiomatic choice for anything that looks like a disk, which our pseudo block device does. Reviewers of real FreeBSD driver patches would push back on a driver that used `g_class` directly when `g_disk` would do.

Second, `g_disk` gives us devstat integration, standard ioctls, and `/dev` node management for free. Re-implementing those by hand would be a substantial distraction from the teaching goal of this chapter.

Third, the simpler the first working driver, the easier it is to reason about. We have plenty of code to write in the next few sections. We do not need to spend pages on class-level GEOM plumbing that `g_disk` already gets right.

That said, if you are curious, you should absolutely read `g_zero.c`. It is a small file and it reveals the mechanics that `g_disk` abstracts. The Wrapping Up for this section will point you to it one last time.

### A Walk Through g_class

For readers who want a little more of the underlying machinery, let us walk through what a `g_class` structure looks like in code, without yet building one of our own.

The following is reproduced (slightly simplified) from `/usr/src/sys/geom/zero/g_zero.c`.

```c
static struct g_class g_zero_class = {
    .name = G_ZERO_CLASS_NAME,
    .version = G_VERSION,
    .start = g_zero_start,
    .init = g_zero_init,
    .fini = g_zero_fini,
    .destroy_geom = g_zero_destroy_geom
};

DECLARE_GEOM_CLASS(g_zero_class, g_zero);
```

`.name` is the class name, used in `geom -t` output. `.version` must match `G_VERSION` for the running kernel; mismatched versions are rejected at load time. `.start` is the function called when a BIO arrives at a provider of this class. `.init` is called when the class is first instantiated, typically to create the initial geom and its provider. `.fini` is the teardown counterpart to `.init`. `.destroy_geom` is called when a specific geom under this class is being removed.

`DECLARE_GEOM_CLASS` is a macro that expands to a module declaration that loads this class into the kernel when the module is loaded. It hides the `moduledata_t`, the `SYSINIT`, and the `g_modevent` wiring behind a single line.

Our driver does not use `g_class` directly. `g_disk` does it for us, and the class it declares under the hood is the universal `DISK` class that all disk-shaped drivers share. But understanding the structure is useful because, if you ever write a transformation class (a GEOM-level encrypt, compress, or partition layer), you will define your own `g_class`.

### The Life of a BIO, In Detail

We covered the BIO life cycle briefly earlier. Here it is in more detail, because every storage-driver bug touches this life cycle at some point.

A BIO originates somewhere above the driver. For our driver, the most common origins are:

1. **A filesystem's buffer-cache write-back**. UFS calls `bwrite` or `bawrite` on a buffer, which builds a BIO and hands it to GEOM through `g_io_request`.
2. **A filesystem's buffer-cache read**. UFS calls `bread`, which checks the cache and, on a miss, issues a BIO.
3. **A raw access through `/dev/myblk0`**. A program calls `read(2)` or `write(2)` on the node. `devfs` and GEOM's character-device integration build a BIO and issue it.
4. **A tool-issued operation**. `newfs_ufs`, `diskinfo`, `dd`, and similar tools issue BIOs the same way as a raw access.

Once built, the BIO is routed through GEOM's topology. Each consumer -> provider hop along the way may transform or validate the BIO. For a simple stack (our driver with no intermediate geoms), there are no intermediate hops; the BIO arrives at our provider and is dispatched to our strategy function.

Inside `g_disk`, the strategy function is preceded by three small pieces of bookkeeping:

1. Some sanity checks (for instance, verifying that the BIO's offset and length are within the media).
2. A call to `devstat_start_transaction_bio` to start timing the request.
3. A call to the driver's `d_strategy`.

On completion, `g_disk` intercepts the `biodone` call, records the end time with `devstat_end_transaction_bio`, and forwards the completion up the stack.

From the driver's point of view, the only thing that matters is that `d_strategy` gets called, and that `biodone` is called once per BIO. Everything else is plumbing.

### Error Propagation

When a BIO fails, the driver sets `bio_error` to an `errno` value and sets the `BIO_ERROR` flag in `bio_flags`. `biodone` is then called as normal.

Above the driver, GEOM's completion code checks for the error. If set, the error is propagated up the stack. The filesystem sees the error and decides what to do; typically, a read error on metadata is fatal and the filesystem reports EIO to user space. A write error is often delayed; the filesystem may retry, or may mark the associated buffer as needing attention on the next sync.

Common `errno` values in the BIO path:

- `EIO`: a generic I/O error. The kernel assumes the device is having trouble.
- `ENXIO`: the device is not configured or has gone away.
- `EOPNOTSUPP`: the driver does not support this operation.
- `EROFS`: the medium is read-only.
- `ENOSPC`: no space available.
- `EFAULT`: an address in the request is invalid. Very rare in the BIO path.

For our in-memory driver, the only errors that should ever appear are the bounds-check error (`EIO`) and the unknown-command error (`EOPNOTSUPP`).

### What g_disk Does That You Do Not See

We have mentioned that `g_disk` takes care of several things on our behalf. Here is a fuller list.

- It creates the GEOM class for the `DISK` type if it does not already exist, and it shares this class among all disk drivers.
- It creates a geom under that class when we call `disk_create`.
- It creates a provider on the geom and publishes it in `/dev`.
- It wires up devstat accounting automatically.
- It handles the GEOM access protocol, converting user-space `open` and `close` calls on `/dev/myblk0` into provider access-count changes.
- It handles the GEOM character-device interface, converting read and write on `/dev/myblk0` into BIOs to our strategy function.
- It handles the BIO_GETATTR default cases (most attributes have sensible defaults).
- It handles withering on `disk_destroy`, waiting for in-flight BIOs.
- It forwards `d_ioctl` calls for ioctls it does not handle itself.

Each of these is a piece of code you would have to write if you built directly on `g_class`. Reading `/usr/src/sys/geom/geom_disk.c` is a good way to appreciate just how much `g_disk` does for us.

### Inspecting Our Provider

Let us take our skeleton driver from Section 3, load it, and inspect it through GEOM's eyes.

```console
# kldload ./myblk.ko
# geom disk list myblk0
Geom name: myblk0
Providers:
1. Name: myblk0
   Mediasize: 1048576 (1.0M)
   Sectorsize: 512
   Mode: r0w0e0
   descr: (null)
   ident: (null)
   rotationrate: unknown
   fwsectors: 0
   fwheads: 0
```

`geom disk list` shows us only the `DISK` class's geoms. Each of those geoms has one provider. We can also see the full class tree.

```console
# geom -t | head -n 40
Geom        Class      Provider
ada0        DISK       ada0
 ada0p1     PART       ada0p1
 ada0p2     PART       ada0p2
 ada0p3     PART       ada0p3
myblk0      DISK       myblk0
```

Our geom is a sibling of the real disks, without any upper-layer class attached to it yet. In later sections we will see what happens when a filesystem attaches.

```console
# geom stats myblk0
```

`geom stats` returns detailed performance counters. On an idle, unused device like ours, all the counters are zero.

```console
# gstat -I 1
dT: 1.002s  w: 1.000s
 L(q)  ops/s    r/s   kBps   ms/r    w/s   kBps   ms/w    %busy Name
    0      0      0      0    0.0      0      0    0.0    0.0| ada0
    0      0      0      0    0.0      0      0    0.0    0.0| myblk0
```

`gstat` is a more compact view that updates live. We will use this heavily in later sections.

### Wrapping Up Section 4

GEOM is a composable block-layer framework made of classes, geoms, providers, and consumers. Requests flow through it as `struct bio` objects, with `BIO_READ`, `BIO_WRITE`, and a handful of other commands. Access rights, topology locking, and event-driven structure management are the mechanisms that keep the framework safe to evolve under load. `g_disk` wraps all of this for disk-shaped drivers and gives them a friendlier interface with little loss of expressiveness.

Our skeleton driver is now a first-class GEOM participant, even though it cannot yet do any real I/O. In the next section, we will give it that missing piece. We will allocate a backing buffer, implement a strategy function that actually reads and writes, and watch the kernel's storage stack exercise our code from both raw-access and filesystem-access directions.

## Section 5: Implementing Basic Read and Write

In Section 3 we returned `ENXIO` for every BIO. In Section 4 we learned enough about GEOM to know exactly what kind of request our strategy function receives and what its obligations are. In this section we will replace that placeholder with a working handler that reads and writes real bytes against an in-memory backing store. By the end, our driver will serve traffic through `dd`, will return sane data, and will survive being formatted by `newfs_ufs`.

### The Backing Store

Our backing store for now is simply an array of bytes in kernel memory, sized to match `d_mediasize`. It is the simplest possible representation of a disk: a flat buffer. Real storage drivers replace this with hardware DMA, with a vnode-backed file, or with a swap-backed VM object, but a flat buffer is enough to teach every other concept in this chapter without distraction.

For 1 MiB we can simply `malloc` the buffer. For larger sizes we would need a different allocator, because the kernel heap does not scale gracefully to contiguous allocations of tens or hundreds of megabytes. `md(4)` avoids the issue for large memory disks by using page-at-a-time allocation and a custom indirection structure. We do not need that level of sophistication yet, but we will note the limitation in the code.

Let us update `myblk_softc` to include the backing store.

```c
struct myblk_softc {
    struct disk     *disk;
    struct mtx       lock;
    u_int            unit;
    uint8_t         *backing;
    size_t           backing_size;
};
```

Two new fields: `backing` is the pointer to the kernel memory we allocated, and `backing_size` is the number of bytes we allocated. These should always be equal to `d_mediasize`, but storing the size explicitly is cleaner than relying on indirection through `disk->d_mediasize`.

Now, in `myblk_attach_unit`, allocate the backing buffer.

```c
static int
myblk_attach_unit(struct myblk_softc *sc)
{

    sc->backing_size = MYBLK_MEDIASIZE;
    sc->backing = malloc(sc->backing_size, M_MYBLK, M_WAITOK | M_ZERO);

    sc->disk = disk_alloc();
    sc->disk->d_name       = MYBLK_NAME;
    sc->disk->d_unit       = sc->unit;
    sc->disk->d_strategy   = myblk_strategy;
    sc->disk->d_sectorsize = MYBLK_SECTOR;
    sc->disk->d_mediasize  = MYBLK_MEDIASIZE;
    sc->disk->d_maxsize    = MAXPHYS;
    sc->disk->d_drv1       = sc;

    disk_create(sc->disk, DISK_VERSION);
    return (0);
}
```

`malloc` with `M_WAITOK | M_ZERO` returns a zeroed buffer or sleeps until one is available. It cannot fail for small allocations on a healthy system, which is why we do not check the return value here. If we were allocating a very large buffer we might want `M_NOWAIT` and explicit error handling, but for 1 MiB `M_WAITOK` is the idiomatic choice.

`myblk_detach_unit` must free the backing store after destroying the disk.

```c
static void
myblk_detach_unit(struct myblk_softc *sc)
{

    if (sc->disk != NULL) {
        disk_destroy(sc->disk);
        sc->disk = NULL;
    }
    if (sc->backing != NULL) {
        free(sc->backing, M_MYBLK);
        sc->backing = NULL;
        sc->backing_size = 0;
    }
}
```

Order matters here. We destroy the disk first, which ensures there are no more BIOs in flight. Only then do we free the backing buffer. If we freed the buffer first, an in-flight BIO might try to `memcpy` into or out of a pointer that no longer refers to our memory, and the kernel would crash on the next I/O.

### The Strategy Function

Now for the heart of the change. Replace the placeholder `myblk_strategy` with a function that actually services BIOs.

```c
static void
myblk_strategy(struct bio *bp)
{
    struct myblk_softc *sc;
    off_t offset;
    size_t len;

    sc = bp->bio_disk->d_drv1;
    offset = bp->bio_offset;
    len = bp->bio_bcount;

    if (offset < 0 ||
        offset > sc->backing_size ||
        len > sc->backing_size - offset) {
        bp->bio_error = EIO;
        bp->bio_flags |= BIO_ERROR;
        bp->bio_resid = len;
        biodone(bp);
        return;
    }

    switch (bp->bio_cmd) {
    case BIO_READ:
        mtx_lock(&sc->lock);
        memcpy(bp->bio_data, sc->backing + offset, len);
        mtx_unlock(&sc->lock);
        bp->bio_resid = 0;
        break;

    case BIO_WRITE:
        mtx_lock(&sc->lock);
        memcpy(sc->backing + offset, bp->bio_data, len);
        mtx_unlock(&sc->lock);
        bp->bio_resid = 0;
        break;

    case BIO_DELETE:
        mtx_lock(&sc->lock);
        memset(sc->backing + offset, 0, len);
        mtx_unlock(&sc->lock);
        bp->bio_resid = 0;
        break;

    case BIO_FLUSH:
        /*
         * In-memory backing store is always "flushed".
         * Nothing to do.
         */
        bp->bio_resid = 0;
        break;

    default:
        bp->bio_error = EOPNOTSUPP;
        bp->bio_flags |= BIO_ERROR;
        bp->bio_resid = len;
        break;
    }

    biodone(bp);
}
```

Let us read this carefully. It is not a long function, but every line is doing something that matters.

The first line finds our softc. GEOM gives us the BIO with a pointer to the disk in `bp->bio_disk`. We stashed our softc in `d_drv1` during `disk_create`, so we retrieve it from there. This is the block-driver equivalent of `device_get_softc(dev)` in the Newbus world.

The second pair of lines extract the offset and length of the request. `bio_offset` is a byte offset into the media. `bio_bcount` is the number of bytes to transfer. GEOM has already translated file-level operations through whatever layers sit above us into a linear byte range.

The bounds check that follows is defensive programming. GEOM will not normally send us a request that exceeds the media size, because it splits and validates BIOs on our behalf. But defensive drivers check anyway, because a silently accepted out-of-bounds write can smash kernel memory, and because the cost of the check is a few instructions per request. We also guard against arithmetic overflow by rewriting the obvious `offset + len > backing_size` check as `len > backing_size - offset`, which cannot overflow because `offset <= backing_size` at this point.

The switch is where the real work happens. Each BIO command gets its own case.

`BIO_READ` copies `len` bytes from our backing store at `offset` into `bp->bio_data`. GEOM has allocated `bp->bio_data` for us, and it will be released when the BIO completes. Our job is just to fill it.

`BIO_WRITE` copies `len` bytes from `bp->bio_data` into our backing store at `offset`. Symmetrical to the read case.

`BIO_DELETE` zeroes the range. For a real disk, `BIO_DELETE` is how filesystems signal that a range of blocks is no longer in use, and the disk is free to reclaim it. SSDs use it to drive TRIM. For our in-memory driver, there is nothing to reclaim, but zeroing the range is a reasonable response because it reflects the "data is gone" semantics.

`BIO_FLUSH` is a request to commit outstanding writes to stable storage. Our storage is never volatile in the sense that a FLUSH would help: every `memcpy` is already visible to the next `memcpy` in the same order it was issued. We return success with nothing to do.

Any other command we do not recognise gets `EOPNOTSUPP`. GEOM layers above us will see this and react accordingly.

At the end, `biodone(bp)` completes the BIO. This is not optional. Every BIO that enters the strategy function must leave through `biodone` exactly once, or the BIO will be leaked, the caller will block forever, and you will have a difficult time diagnosing the issue.

### The Role of bio_resid

Notice the handling of `bp->bio_resid`. This field represents the number of bytes remaining to transfer after the driver is done. When the full transfer succeeds, `bio_resid` is zero. When the transfer fails completely, `bio_resid` equals `bio_bcount`. When the transfer partially succeeds, `bio_resid` is the number of bytes that did not make it.

Our driver either transfers everything or nothing, so we set `bio_resid` to either `0` (success) or `len` (error). A real hardware driver might set it to an intermediate value if a transfer stopped partway through. Filesystems and user-space tools use `bio_resid` to figure out how much data actually moved.

### The Lock

We take `sc->lock` around the `memcpy`. For an in-memory driver that services one request at a time, the lock is not doing much visible work: the kernel's BIO scheduling makes truly concurrent requests unlikely on our toy device. But the lock is good hygiene. GEOM does not promise that your strategy function will be invoked serially, and even if it did, a future change to the driver to add an asynchronous worker thread would require the lock anyway. Adding it now is cheaper than adding it later.

A more sophisticated driver might use a fine-grained lock, or might use an MPSAFE approach that relies on atomic operations. For now, a coarse mutex around the `memcpy` is fine. It is correct, it is easy to reason about, and it does not hurt performance on a pseudo device.

### Rebuilding and Reloading

After updating the source and `kldunload`-ing the old version, rebuild and reload.

```console
# make
cc -O2 -pipe -fno-strict-aliasing ...
# kldunload myblk
# kldload ./myblk.ko
# dmesg | tail -n 1
myblk: loaded, /dev/myblk0 size=1048576 bytes
```

Now let us try some real I/O.

```console
# dd if=/dev/zero of=/dev/myblk0 bs=4096 count=16
16+0 records in
16+0 records out
65536 bytes transferred in 0.001104 secs (59 MB/sec)
# dd if=/dev/myblk0 of=/dev/null bs=4096 count=16
16+0 records in
16+0 records out
65536 bytes transferred in 0.000512 secs (128 MB/sec)
```

We wrote 64 KiB of zeros and read them back. The speeds you see will depend on your hardware and on how much the buffer cache helps, but any speed above a few MB/sec is fine for a first run.

```console
# dd if=/dev/random of=/dev/myblk0 bs=4096 count=16
16+0 records in
16+0 records out
65536 bytes transferred in 0.001233 secs (53 MB/sec)
# dd if=/dev/myblk0 of=pattern.bin bs=4096 count=16
16+0 records in
16+0 records out
# dd if=/dev/myblk0 of=pattern2.bin bs=4096 count=16
16+0 records in
16+0 records out
# cmp pattern.bin pattern2.bin
#
```

We wrote random data, read it back twice, and confirmed that both reads return the same content. Our driver is now a coherent store.

### A Quick Look Under Load

Let us run a short stress test and watch `gstat`.

In one terminal:

```console
# while true; do dd if=/dev/urandom of=/dev/myblk0 bs=4096 \
    count=256 2>/dev/null; done
```

In another terminal:

```console
# gstat -I 1 -f myblk0
dT: 1.002s  w: 1.000s
 L(q)  ops/s    r/s   kBps   ms/r    w/s   kBps   ms/w    %busy Name
    0    251      0      0    0.0    251   1004    0.0    2.0| myblk0
```

About 250 write operations per second at 4 KiB each, approximately 1 MB/sec. The latency is very low because the backing store is RAM. For a real disk the numbers would be very different, but the structure of what you are watching is the same.

Stop the stress test with `Ctrl-C` on the first terminal.

### Refining the Driver with ioctl Support

Many storage tools send an ioctl to the device to query geometry or to issue commands. GEOM handles the common ones for us, but if we provide a `d_ioctl` callback, the kernel will route unknown ioctls through to our function. For now we do not implement any custom ioctl. We only note that the hook exists.

```c
static int
myblk_ioctl(struct disk *d, u_long cmd, void *data, int flag,
    struct thread *td)
{

    (void)d; (void)data; (void)flag; (void)td;

    switch (cmd) {
    /* No custom ioctls yet. */
    default:
        return (ENOIOCTL);
    }
}
```

We register the callback by assigning `sc->disk->d_ioctl = myblk_ioctl;` before calling `disk_create`. Returning `ENOIOCTL` from the default case tells GEOM that we do not handle the command and gives it the chance to pass the request to its own default handler.

### Refining the Driver with getattr Support

GEOM uses `BIO_GETATTR` to ask storage devices for named attributes. A filesystem might ask for `GEOM::rotation_rate` to know whether it is on spinning media. The partitioning layer might ask for `GEOM::ident` to get a stable identifier. A `d_getattr` callback is the hook that lets us respond.

```c
static int
myblk_getattr(struct bio *bp)
{
    struct myblk_softc *sc;

    sc = bp->bio_disk->d_drv1;

    if (strcmp(bp->bio_attribute, "GEOM::ident") == 0) {
        if (bp->bio_length < sizeof("MYBLK0"))
            return (EFAULT);
        strlcpy(bp->bio_data, "MYBLK0", bp->bio_length);
        bp->bio_completed = strlen("MYBLK0") + 1;
        return (0);
    }

    /* Let g_disk fall back to default behaviour. */
    (void)sc;
    return (-1);
}
```

The return-value convention for `d_getattr` is worth pausing on, because it trips up many first-time readers. Returning `0` with `bio_completed` set tells `g_disk` that we handled the attribute successfully. Returning a positive errno value (such as `EFAULT` for a too-small buffer) tells `g_disk` that we handled the attribute but the operation failed. Returning `-1` tells `g_disk` that we did not recognise the attribute and it should try its built-in default handler. That is why we return `-1` at the bottom: we want `g_disk` to answer standard attributes such as `GEOM::fwsectors` on our behalf. For our driver, responding to `GEOM::ident` with a short string is enough to show up in `diskinfo -v`. Register this with `sc->disk->d_getattr = myblk_getattr;` before `disk_create`.

### Partial Writes and Short Reads

Our driver does not actually produce partial writes or short reads, because the backing store is in RAM and every transfer either fully succeeds or fully fails. But for a real hardware driver, partial transfers are normal: a disk may return a few sectors successfully and then fail on a bad sector. The BIO framework supports this through `bio_resid`, and a driver should set `bio_resid` to the number of bytes that did not complete.

The practical guidance is to always set `bio_resid` explicitly before calling `biodone`. If the transfer fully succeeded, set it to zero. If it partially succeeded, set it to the residual. If it fully failed, set it to `bio_bcount`. Forgetting to set `bio_resid` leaves whatever garbage was in the field when the BIO was allocated, which can confuse callers.

### Common Mistakes in Strategy Functions

Before we continue, let us name three common mistakes that appear in first-time strategy functions.

**Forgetting `biodone`.** Every path out of the strategy function must call `biodone(bp)` on the BIO. If you forget, the BIO is leaked and the caller hangs. This is the single most common source of "my mount hangs" problems.

**Holding a lock across `biodone`.** `biodone` may call upward into GEOM or into a filesystem's completion handler. Those handlers may take other locks, or may need to acquire locks you already hold, leading to lock-order reversal and potential deadlock. The safest pattern is to drop your lock before calling `biodone`. Our simple version does this implicitly: the `mtx_unlock` is always inside the switch, and `biodone` runs after the switch.

**Returning from the strategy function with an error code.** `d_strategy` is a `void` function. Errors are reported by setting `bio_error` and the `BIO_ERROR` flag on the BIO, not by returning. Compilers catch this if you declare the function correctly, but beginners sometimes write it as returning `int`, which causes compiler warnings that should not be ignored.

### Chained BIOs and BIO Hierarchies

A BIO can have a child. GEOM uses this when a transformation class needs to split, combine, or transform a request into one or more downstream requests. For example, a mirror class might take a BIO_WRITE and issue two child BIOs, one to each mirror member. A partition class might take a BIO_READ and issue a single child BIO with the offset shifted into the underlying provider's address space.

The parent-child relationship is recorded in `bio_parent`. When a child completes, its error is propagated to the parent by `biodone`, which accumulates errors and delivers the parent when all children have completed.

Our driver does not produce child BIOs. It receives them as leaves of the chain. From the driver's perspective, every BIO is self-contained: it has an offset, a length, and a data buffer, and our job is to service it.

But if you ever find yourself needing to split a BIO inside your driver (for example, if a request spans a boundary that your backing store handles in separate chunks), you can use `g_clone_bio` to create a child BIO, `g_io_request` to dispatch it, and `g_std_done` or a custom completion handler to reassemble the parent. The pattern is visible in several places in the kernel, including `g_mirror` and `g_raid`.

### The Thread Context of the Strategy Function

The strategy function runs in whatever thread submitted the BIO. For filesystem-originated BIOs, that is typically the filesystem's syncer thread or a buffer-cache worker. For direct user-space access, it is the user thread that called `read` or `write` on `/dev/myblk0`. For GEOM transformations, it might be the GEOM event thread or a class-specific worker thread.

What this means for your driver is that `d_strategy` can run in many different thread contexts. You cannot assume that `curthread` belongs to any particular process, and you cannot block for a long time or the calling filesystem (or user program) will stall.

If your strategy function needs to do something slow (I/O against a vnode, waiting for hardware, or complex locking), the right pattern is to enqueue the BIO on an internal queue and have a dedicated worker thread process it. This is what `md(4)` does for all backing types, because vnode I/O (for instance) can block arbitrarily long.

Our driver is entirely in-memory and does `memcpy` only, so we do not need a worker thread. But understanding the pattern is important for the future.

### A Worked Example: Reading Across a Boundary

Suppose a filesystem issues a BIO_READ with offset 100000 and length 8192. That spans bytes 100000 through 108191. Let us trace how our strategy function handles it.

1. `bp->bio_cmd` is `BIO_READ`.
2. `bp->bio_offset` is 100000.
3. `bp->bio_bcount` is 8192.
4. `bp->bio_data` points to a kernel buffer (or a user buffer mapped into the kernel) where the 8192 bytes should go.

Our code computes `offset = 100000` and `len = 8192`. The bounds check passes: `100000 + 8192 = 108192`, which is less than our `backing_size` of 32 MiB (33554432).

The switch enters the `BIO_READ` case. We acquire the lock, `memcpy` 8192 bytes from `sc->backing + 100000` into `bp->bio_data`, and release the lock. We set `bp->bio_resid = 0` to indicate a complete transfer. We fall through to `biodone(bp)`, which completes the BIO.

The filesystem receives the completion, notices the error is zero, and uses the 8192 bytes. The read is complete.

Now suppose, instead, the offset was 33554431 and the length was 2 bytes. That is one byte inside the backing store and one byte past the end.

1. `offset = 33554431`.
2. `len = 2`.

The bounds check: `offset > sc->backing_size` evaluates `33554431 > 33554432`, which is false. `len > sc->backing_size - offset` evaluates `2 > 33554432 - 33554431`, which evaluates to `2 > 1`, which is true. The check fails, and we fall into the error path: set `bio_error = EIO`, set the `BIO_ERROR` flag, set `bio_resid = 2`, and call `biodone`. The filesystem sees the error and handles it.

Notice how we used subtraction to avoid the overflow risk. Had we written `offset + len > sc->backing_size`, and had `offset` and `len` both been close to the maximum of `off_t`, the addition could wrap around to a small number and the check would silently pass for a malformed request. Defensive bounds checks always rearrange arithmetic to avoid overflow.

### The Devstat Side Effect

One pleasant feature of using `g_disk` is that devstat accounting is automatic. Every BIO we service is counted by `iostat` and `gstat`. No extra code is needed.

You can verify this with `iostat -x 1` in another terminal while running the stress loop.

```text
                        extended device statistics
device     r/s     w/s    kr/s    kw/s  ms/r  ms/w  ms/o  ms/t qlen  %b
ada0         0       2       0      48   0.0   0.1   0.0   0.1    0   0
myblk0       0     251       0    1004   0.0   0.0   0.0   0.0    0   2
```

If our driver were built on the raw `g_class` API rather than `g_disk`, we would have to wire up devstat ourselves. This is one of the small quality-of-life features that `g_disk` gives us for free.

### Wrapping Up Section 5

We replaced the placeholder strategy function with a working handler. Our driver now services `BIO_READ`, `BIO_WRITE`, `BIO_DELETE`, and `BIO_FLUSH` correctly against an in-memory backing store. It participates in devstat, it cooperates with `gstat`, and it accepts real traffic from `dd`.

In the next section, we will cross the boundary from raw block access to filesystem access. We will format the device with `newfs_ufs`, mount it, create files on it, and observe how the request path changes when a real filesystem sits above the provider.

## Section 6: Mounting a Filesystem on the Device

Up to this point, our driver has been exercised through raw access: `dd`, `diskinfo`, and similar tools reading and writing the entire surface as a flat byte range. That is a valuable mode, but it is not the mode most storage devices live in. Storage devices in real life serve filesystems. This section takes our driver the last mile: we will format it, mount a real filesystem on it, create files, and observe how the kernel's block-layer plumbing routes requests when a filesystem is in the picture.

This is also the first section where the theoretical distinction between raw access and filesystem access becomes concrete. Understanding the difference, and being able to see it in action, is one of the most useful insights a storage-driver author can acquire.

### The Plan

We will do the following in this section, in order.

1. Increase the media size of our driver from 1 MiB to something large enough to hold a usable UFS filesystem.
2. Build and load the updated driver.
3. Run `newfs_ufs` against the device to create a filesystem.
4. Mount the filesystem on a scratch directory.
5. Create some files and verify that the data is read back correctly.
6. Unmount the filesystem.
7. Reload the module and watch what happens.

By the end, you will have seen a complete filesystem on top of your own block driver.

### Increasing the Media Size

UFS has a minimum practical size. You can create tiny UFS filesystems, but the overhead of the superblock, cylinder groups, and inode tables takes a noticeable fraction of the space on anything smaller than a few megabytes. For our purposes, 32 MiB is a comfortable size: it is small enough that the backing store still fits in a plain `malloc`, and large enough that UFS has room to breathe.

Update the size definitions at the top of `myfirst_blk.c`.

```c
#define MYBLK_SECTOR     512
#define MYBLK_MEDIASIZE  (32 * 1024 * 1024)   /* 32 MiB */
```

Rebuild.

```console
# make clean
# make
# kldunload myblk
# kldload ./myblk.ko
# diskinfo -v /dev/myblk0
/dev/myblk0
        512             # sectorsize
        33554432        # mediasize in bytes (32M)
        65536           # mediasize in sectors
        0               # stripesize
        0               # stripeoffset
```

32 MiB is enough.

### Formatting with newfs_ufs

`newfs_ufs` is the standard UFS formatter on FreeBSD. It lays down the superblock, the cylinder groups, the root inode, and all the other structures a UFS filesystem requires. Let us run it on our device.

```console
# newfs_ufs /dev/myblk0
/dev/myblk0: 32.0MB (65536 sectors) block size 32768, fragment size 4096
        using 4 cylinder groups of 8.00MB, 256 blks, 1280 inodes.
super-block backups (for fsck_ffs -b #) at:
192, 16576, 32960, 49344
```

A few things happened under the hood.

`newfs_ufs` opened `/dev/myblk0` for writing, which caused the GEOM access count to tick up. Our strategy function then received a stream of writes: the superblock first, then the cylinder groups, then the empty root directory, then the several backup superblocks. Each of those writes is a BIO, and each BIO was handled by our driver.

You can verify that `newfs_ufs` really wrote to the device by reading a few bytes back.

```console
# dd if=/dev/myblk0 bs=1 count=16 2>/dev/null | hexdump -C
00000000  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00
```

The first few bytes of a UFS partition are deliberately zero because the superblock does not sit at offset zero: it is at offset 65536 (block 128) to leave room for boot blocks and other preambles. Let us peek there.

```console
# dd if=/dev/myblk0 bs=512 count=2 skip=128 2>/dev/null | hexdump -C | head
00010000  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00
00010010  80 00 00 00 80 00 00 00  a0 00 00 00 00 00 00 00
...
```

You should see non-zero bytes now. That is the superblock that `newfs_ufs` laid down on our backing store.

### Mounting the Filesystem

Create a mount point and mount the filesystem.

```console
# mkdir -p /mnt/myblk
# mount /dev/myblk0 /mnt/myblk
# mount | grep myblk
/dev/myblk0 on /mnt/myblk (ufs, local)
# df -h /mnt/myblk
Filesystem    Size    Used   Avail Capacity  Mounted on
/dev/myblk0    31M    8.0K     28M     0%    /mnt/myblk
```

Our pseudo device is now a real filesystem. Watch the GEOM access counts.

```console
# geom disk list myblk0 | grep Mode
   Mode: r1w1e1
```

`r1w1e1` means one reader, one writer, one exclusive holder. The exclusive hold is UFS: it has told GEOM that it is the sole authority over writes to the device until it is unmounted.

### Creating and Reading Files

Let us actually use the filesystem.

```console
# echo "hello from myblk" > /mnt/myblk/hello.txt
# ls -l /mnt/myblk
total 4
-rw-r--r--  1 root  wheel  17 Apr 19 18:17 hello.txt
# cat /mnt/myblk/hello.txt
hello from myblk
```

Note what just happened. The call `echo "hello from myblk" > /mnt/myblk/hello.txt` traveled through the system call layer to `sys_openat`, then to VFS, then to UFS, which opened the root directory's inode, created a new inode for `hello.txt`, allocated a data block, copied the 17 bytes into the buffer cache, and scheduled a write-back. The buffer cache eventually called down to GEOM, which called down to our strategy function, which copied those bytes into our backing store.

When you then ran `cat`, the request traveled down the same stack. Except, because the data was still in the buffer cache from the recent write, UFS did not actually need to read from our device. The buffer cache served the read from RAM. If you unmount and remount, you will see an actual read.

```console
# umount /mnt/myblk
# mount /dev/myblk0 /mnt/myblk
# cat /mnt/myblk/hello.txt
hello from myblk
```

That second `cat` probably did cause BIO_READ requests to reach our driver, because the unmount-and-remount cycle invalidated the buffer cache for that filesystem.

### Watching the Traffic

`gstat` shows us the BIO traffic in real time. Open another terminal and run `gstat -I 1 -f myblk0`. Then in the first terminal, create a big file.

```console
# dd if=/dev/zero of=/mnt/myblk/big bs=1m count=16
16+0 records in
16+0 records out
16777216 bytes transferred in 0.150 secs (112 MB/sec)
```

In the `gstat` terminal, you should see a burst of writes, perhaps spread across a second or two depending on how quickly the buffer cache flushes.

```text
 L(q)  ops/s    r/s   kBps   ms/r    w/s   kBps   ms/w    %busy Name
    0    128      0      0    0.0    128  16384    0.0   12.0| myblk0
```

These are the 4 KiB or 32 KiB (depending on UFS's block size) writes that UFS is issuing to fill the file. We can verify the file's presence.

```console
# ls -lh /mnt/myblk
total 16460
-rw-r--r--  1 root  wheel    16M Apr 19 18:19 big
-rw-r--r--  1 root  wheel    17B Apr 19 18:17 hello.txt
# du -ah /mnt/myblk
 16M    /mnt/myblk/big
4.5K    /mnt/myblk/hello.txt
 16M    /mnt/myblk
```

And we can delete it again to watch the BIO_DELETE traffic.

```console
# rm /mnt/myblk/big
```

UFS by default does not issue `BIO_DELETE` unless the filesystem was mounted with the `trim` option, so on a plain mount you will see almost no BIO traffic on delete: UFS just marks the blocks as free in its own metadata. To see `BIO_DELETE`, we would need to mount with `-o trim`, which we will cover briefly in the labs.

### Unmounting

Unmount the filesystem before unloading the module.

```console
# umount /mnt/myblk
# geom disk list myblk0 | grep Mode
   Mode: r0w0e0
```

The access count dropped back to zero as soon as UFS released its exclusive hold. Our driver is now free to be unloaded or further mucked with.

### Attempting to Unload While Mounted

What happens if you forget the `umount` and try to unload the module?

```console
# mount /dev/myblk0 /mnt/myblk
# kldunload myblk
kldunload: can't unload file: Device busy
```

The kernel refuses. The `g_disk` subsystem knows that our provider still has an active exclusive holder, and it will not let `disk_destroy` proceed until the hold is released. This is the same mechanism we saw in Chapter 26 protecting the USB serial device during an active session, lifted to the GEOM layer.

This is a safety feature. Unloading the module while a filesystem is mounted on the backing device would cause the kernel to panic on the next BIO: the strategy function would no longer exist, but UFS would still try to call into it.

Unmount first, then unload.

```console
# umount /mnt/myblk
# kldunload myblk
# kldstat | grep myblk
# 
```

Clean.

### A Brief Anatomy of UFS on Top of Our Driver

Now that we have UFS mounted on our device, it is worth pausing to notice what is actually on the backing store. UFS is a well-documented filesystem, and seeing its structures in place on a device we control is illuminating.

The first 65535 bytes of a UFS filesystem are reserved for the boot area. On our device, these bytes are all zero because `newfs_ufs` does not write a boot sector by default.

At offset 65536 lives the superblock. The superblock is a fixed-size structure that describes the geometry of the filesystem: the block size, the fragment size, the number of cylinder groups, the location of the root inode, and many other invariants. `newfs_ufs` writes the superblock first, and it also writes backup copies at predictable offsets in case the primary is corrupted.

Following the superblock come the cylinder groups. Each cylinder group holds inodes, data blocks, and metadata for a chunk of the filesystem's address space. The number and size of cylinder groups depends on the filesystem size. Our 32 MiB filesystem has four cylinder groups of 8 MiB each.

Within each cylinder group sit inode blocks. Each inode is a small structure (256 bytes on FreeBSD UFS2) that describes a single file or directory: its type, owner, permissions, timestamps, size, and the block addresses of its data.

Finally, the data blocks themselves hold file contents. These are allocated from the free-block map in the cylinder group.

When we wrote `"hello from myblk"` into `/mnt/myblk/hello.txt`, the kernel did roughly the following:

1. VFS asked UFS to create a new file `hello.txt` in the root directory.
2. UFS allocated an inode from the root cylinder group's inode table.
3. UFS updated the root directory's inode to include an entry for `hello.txt`.
4. UFS allocated a data block for the file.
5. UFS wrote the 17 bytes of content into that data block.
6. UFS wrote the updated inode back.
7. UFS wrote the updated directory entry back.
8. UFS updated its internal bookkeeping.

Each of those steps turned into one or more BIOs to our driver. Most were small writes on metadata blocks. The file content itself was one BIO. UFS's Soft Updates feature orders the writes to ensure crash consistency.

If you want to see these BIOs in action, run your DTrace one-liner from Lab 7 while creating a file. You will see a small burst of writes around the time of the `echo`.

### How Mount Actually Works

The `mount(8)` command is a wrapper around the `mount(2)` system call. That system call takes a filesystem type, a source device, and a target mount point, and it asks the kernel to perform the mount.

The kernel's response is to find the appropriate filesystem code by type (UFS, ZFS, tmpfs, etc.) and to call its mount handler, which in UFS's case is `ufs_mount` in `/usr/src/sys/ufs/ffs/ffs_vfsops.c`. The mount handler validates the source, opens it as a GEOM consumer, reads the superblock, verifies that it is well-formed, allocates an in-memory mount structure, and installs it in the namespace.

From our driver's point of view, none of this is visible. We see a series of BIOs: first a few reads for the superblock, then whatever UFS needs to bootstrap its in-memory state. Once mount has succeeded, UFS issues BIOs on its own schedule as the filesystem is used.

If mount fails, UFS reports an error and the kernel's mount code cleans up. The GEOM consumer is detached, the access count drops, and the namespace is left alone. Our driver does not need to do anything special on mount failure.

### The GEOM Character Interface

Earlier in the chapter we said that raw access through `/dev/myblk0` goes through "GEOM's character interface". Here is what that means in more detail.

GEOM publishes a character device for every provider. This is not the same as a `cdev` created with `make_dev`; it is a specialised path within GEOM that presents a provider as a character device to `devfs`. The code for this lives in `/usr/src/sys/geom/geom_dev.c`.

When a user program opens `/dev/myblk0`, `devfs` routes the `open` to GEOM's character-interface code, which attaches a consumer to our provider with the requested access mode. When the program writes, GEOM's character-interface code builds a BIO and issues it to our provider, which routes it to our strategy function. When the program closes the file descriptor, GEOM detaches the consumer, releasing the access.

The character-interface layer translates between `struct uio` (the user-space I/O descriptor) and `struct bio` (the block-layer I/O descriptor). It splits large user I/O into multiple BIOs when necessary, respecting the `d_maxsize` we specified.

All of this is invisible to our driver. We just receive BIOs. But knowing that the character interface exists helps you understand why certain user-space operations map to certain BIO patterns, and why `d_maxsize` matters.

### What Filesystems Need from a Block Driver

Now that we have actually mounted a filesystem on our driver, we can describe more precisely what a filesystem requires from a block driver underneath.

A filesystem needs **correct reads and writes**. If a write at offset X is followed by a read at offset X, the read must return what the write put there, up to the granularity of the sector size. We guaranteed this with `memcpy` into and out of our backing store.

A filesystem needs **correct bounds**. The block driver must not accept reads or writes that extend beyond the media size. We check this explicitly in the strategy function.

A filesystem needs **stable media size**. The size of the device must not change under the filesystem's feet once it is mounted, because filesystem metadata encodes offsets and counts that assume a fixed size. Our driver holds the media size constant.

A filesystem needs **crash safety**, to the extent the underlying storage provides it. UFS can recover from an unclean shutdown if the backing store does not lose previously committed writes. Our RAM-backed driver loses everything on reboot, but it is at least self-consistent while running. In Section 7, we will introduce options for persistence.

A filesystem sometimes needs **flush semantics**. A call to `BIO_FLUSH` should ensure that all previously issued writes are durable before returning. Our RAM-backed driver trivially satisfies this, because there is no deferred writeback in its path.

Finally, a filesystem benefits from **fast sequential access**. This is a quality-of-service matter rather than a correctness matter, but our driver is fine in this regard because `memcpy` is fast.

### Raw Access Versus Filesystem Access, Visualised

Let us draw the two access paths side by side, using our actual driver as the anchor.

```text
Raw access:                          Filesystem access:

  dd(1)                                cat(1)
   |                                    |
   v                                    v
  open("/dev/myblk0")                  open("/mnt/myblk/hello.txt")
   |                                    |
   v                                    v
  read(fd, ...)                        read(fd, ...)
   |                                    |
   v                                    v
  sys_read                             sys_read
   |                                    |
   v                                    v
  devfs                                VFS
   |                                    |
   v                                    v
  GEOM character                       UFS
  interface                            (VOP_READ, bmap)
   |                                    |
   |                                    v
   |                                   buffer cache
   |                                    |
   v                                    v
  GEOM topology  <--------------------- GEOM topology
   |                                    |
   v                                    v
  myblk_strategy (BIO_READ)            myblk_strategy (BIO_READ)
```

The last two hops are identical. Our strategy function is called exactly the same way regardless of whether the request came from `dd` or from `cat` on a mounted file. This is the great advantage of living at the block layer: we do not need to distinguish between the two paths. The upper layers sort out how to translate file-level operations into block-level operations, and we deal in blocks.

### Watching the Request Path with DTrace

If you want to see the request path explicitly, DTrace can help.

```console
# dtrace -n 'fbt::myblk_strategy:entry { printf("cmd=%d off=%lld len=%u", \
    args[0]->bio_cmd, args[0]->bio_offset, args[0]->bio_bcount); }'
```

With the probe running, do something on the mounted filesystem in another terminal and watch the BIOs arrive. You will see reads come through in 512-byte to 32 KiB chunks, depending on UFS's block size and what operation you performed. Running `dd if=/dev/zero of=/mnt/myblk/test bs=1m count=1` produces a burst of 32 KiB writes.

DTrace is one of the most capable observability tools FreeBSD provides, and it comes alive with storage work because the BIO path is so instrumented. We will use it more in later chapters, but even a one-liner like the above is enough to make the abstract path concrete.

### Wrapping Up Section 6

Our pseudo block device now plays the full role of a storage device: raw access through `dd`, filesystem access through UFS, and safe coexistence with kernel unmount protections. The strategy function we wrote in Section 5 did not need to change at all for UFS to work, because UFS and `dd` share the same block-layer protocol below them.

We have also seen the end-to-end flow: VFS at the top, UFS just below, the buffer cache between, GEOM below that, and our driver at the very bottom. That flow is the same for every storage driver in FreeBSD. You now know how to occupy the bottom of it.

In the next section, we will turn our attention to persistence. A RAM-backed device is convenient for testing but loses its contents on every reload. We will discuss options for making the backing store persistent, what trade-offs each option brings, and how to add one of them to our driver.

## Section 7: Persistence and In-Memory Backing Stores

Our driver is self-consistent while running. If you write a byte at offset X, you can read it back at offset X moments later. If you create a file on the mounted filesystem, you can read it again until you unmount or unload. This is already useful for testing and for short-lived workloads.

It is not, however, durable. Unload the module and the backing buffer is freed. Reboot the machine and every byte vanishes. For a teaching driver that is arguably a feature: it reboots clean, it does not accumulate state across runs, and it cannot silently corrupt a previous session. But understanding the options for making storage persistent is essential for real driver work, so this section walks through the major choices and then shows how to add the simplest kind of persistence to our driver.

### Why Persistence Is Hard

Storage persistence is not just about where the bytes live. It is about three intertwined properties.

**Durability** means that once a write returns, the data is safe against a crash. On a hardware disk, durability is typically coupled with the disk's own cache policy: the write hits the drive's internal buffer, then the platter, then the drive reports completion. `BIO_FLUSH` is the hook that gives filesystems a way to demand flush-to-platter semantics.

**Consistency** means that a read at offset X returns the most recent write at offset X, not some earlier or partial version. Consistency is usually provided by the hardware or by careful locking in the driver.

**Crash safety** means that after an unclean shutdown, the state of the storage is usable. Either it reflects all committed writes, or it reflects a well-defined prefix of them. UFS has SU+J (Soft Updates with Journaling) to help recover from a crash; ZFS uses copy-on-write and atomic transactions. All of that relies on a block layer that behaves predictably.

For a teaching driver, we do not need to address all three with full rigour. We need to understand what the choices are and to pick one that fits our goals.

### The Options

There are four common ways to back a pseudo block device.

**In-memory backing (our current choice)**. Fast, simple, lost on reload. Implemented as a `malloc`'d buffer. Scales poorly past a few MiB because it demands contiguous kernel memory.

**Page-at-a-time in-memory backing**. `md(4)` uses this internally for large memory disks. Instead of one big buffer, the driver keeps an indirection table of page-sized allocations and fills them on demand. This scales to very large sizes and avoids wasting memory on sparse regions, but it is more complex.

**Vnode backing**. The driver opens a file in the host filesystem and uses it as the backing store. `mdconfig -t vnode` is the classic example. Reads and writes go through the host's filesystem, which gives persistence at the cost of speed and of a dependency on the host filesystem's correctness. This is how FreeBSD often boots from a memory-disk image embedded in the kernel: the kernel loads the image, presents it as `/dev/md0`, and the root filesystem runs on it.

**Swap backing**. The driver uses a swap-backed VM object as the backing store. `mdconfig -t swap` uses this. It provides persistence across reboots only to the extent that swap is persistent, which on most systems it is not. But it provides a very large sparse address space without consuming physical memory until touched, which is useful for scratch storage.

For this chapter, we will stick with the in-memory option. It is the simplest, it is enough for the labs, and it demonstrates every other concept cleanly. We will discuss how to switch to vnode-backed storage as an exercise, and we will point to `md(4)` for those who want to see a full-featured implementation.

### Saving and Restoring the Buffer

If we want our device to remember its contents across reloads, without changing the backing approach, we can save the buffer to a file on unload and restore it on load. This is not elegant, but it is direct, and it illustrates the contract clearly: the driver is responsible for getting the backing bytes into memory before the first BIO arrives and for flushing them to safety before the last BIO leaves.

In our case, the mechanics would look like this.

On module load, after allocating the backing buffer but before calling `disk_create`, optionally read a file on the host filesystem into the buffer. On module unload, after `disk_destroy` has completed, optionally write the buffer back to that file.

Doing this cleanly from inside the kernel requires the vnode API. The kernel provides `vn_open`, `vn_rdwr`, and `vn_close`, which together let a module read or write a path in the host filesystem. These are not APIs we want to use casually, because they are not designed for high-throughput I/O from inside a driver, and because they run on whatever filesystem happens to be mounted at that path, which is not always safe. But for a one-shot save and restore at load and unload time, they are acceptable.

For teaching purposes we will not implement this. The correct way to persist a block device's contents is to use a real backing store, not to snapshot a RAM buffer. But understanding the technique helps clarify the contract.

### The Contract With Upper Layers

Whatever your backing store, the contract with upper layers is precise.

**A BIO_WRITE that completes successfully must be visible to all subsequent BIO_READ requests**, regardless of buffering layers. Our in-memory driver satisfies this because `memcpy` is the visible effect.

**A BIO_FLUSH that completes successfully must have made all previously successful BIO_WRITE requests durable**. Our in-memory driver satisfies this trivially because there is no lower layer between our `memcpy` and the backing memory; all writes are "durable" in the sense we can offer. A real disk driver typically issues a cache-flush command to the hardware in response to `BIO_FLUSH`.

**A BIO_DELETE may discard data but must not corrupt neighbouring blocks**. Our in-memory driver satisfies this by zeroing only the requested range. A real SSD driver might issue TRIM for the range; a real HDD driver typically has no hardware support for DELETE and can safely ignore it.

**A BIO_READ must return the media contents or an error; it must not return uninitialised memory, stale cached data from a different transaction, or random bytes**. Our in-memory driver satisfies this by zeroing the backing on allocation and writing only through the strategy function.

If you keep these four rules in mind as you design a new driver, you will avoid nearly every correctness bug that plagues new storage drivers.

### What md(4) Does Differently

The kernel's `md(4)` driver is a mature, multi-type memory-disk driver. It supports five backing types: malloc, preload, swap, vnode, and null. Each type has its own strategy function that knows how to serve requests for that backing kind. Reading `/usr/src/sys/dev/md/md.c` is a valuable follow-up to this chapter because it shows how a real driver handles all of the cases we are glossing over.

A few specific things `md(4)` does that we do not.

`md(4)` uses a dedicated worker thread per unit. Incoming BIOs are queued on the softc, and the worker thread dequeues them one by one and dispatches them. This lets the strategy function be very simple: just enqueue and signal. It also isolates blocking work in the worker, which matters for the vnode backing type because `vn_rdwr` can block.

`md(4)` uses `DEV_BSHIFT` (which is `9`, meaning 512-byte sectors) consistently and uses integer arithmetic rather than floating point to handle offsets. This is standard practice in the block layer.

`md(4)` has a full ioctl surface for configuration. The `mdconfig` tool talks to the kernel through ioctls on `/dev/mdctl`, and the driver supports `MDIOCATTACH`, `MDIOCDETACH`, `MDIOCQUERY`, and `MDIOCRESIZE`. We have not implemented anything comparable, because for our pseudo device the configuration is baked in at compile time.

`md(4)` uses `DISK_VERSION_06`, which is the current version of the `g_disk` ABI. Our driver does the same, through the `DISK_VERSION` macro.

If you want to see a production-quality pseudo block device, `md(4)` is the canonical reference. Almost everything we are building would, in a real driver, grow to resemble the shape of `md(4)` over time.

### A Note on Swap-Backed Memory

One technique worth naming, even though we will not use it here, is swap-backed memory. Instead of a `malloc`'d buffer, a driver can allocate a VM object of type `OBJT_SWAP` and map pages from it on demand. The pages are backed by swap space, which means they can be paged out when the system is under memory pressure and paged back in when touched. This gives you a very large, sparse, on-demand backing store that behaves like RAM when hot and like disk when cold.

`md(4)` uses exactly this approach for its swap-backed memory disks. The swap VM object acts as a backing store that the kernel's VM subsystem manages for us, without the driver needing to allocate contiguous physical memory up front. The `OBJT_SWAP` object can hold terabytes of addressable space on a system with only gigabytes of RAM, because most of that space is never touched.

If you ever need to prototype a block device larger than a few hundred MiB, swap-backed memory is likely the right tool. The VM API for it lives in `/usr/src/sys/vm/swap_pager.c`. Reading it is not light work, but it is educational.

### A Note on Preloaded Images

FreeBSD has a mechanism called **preloaded modules**. During boot, the loader can bring in not just kernel modules but also arbitrary data blobs, which are made available to the kernel through `preload_fetch_addr` and `preload_fetch_size`. `md(4)` uses this to expose preloaded filesystem images as `/dev/md*` devices, which is one of the ways FreeBSD can boot entirely from a memory-disk root.

Preloaded images are not a persistence mechanism per se. They are a way to ship data with a kernel module. But they are often used in embedded systems, where the root filesystem is too precious to live on writable storage.

### A Small Extension: Persisting Across Module Reloads Only

We are not going to add real persistence to our driver, but this is a good moment to talk about what it would actually take to make a backing store survive a module unload and reload within the same kernel boot. The naive first idea, and one that beginners reach for quickly, is to put the backing pointer in a file-scope `static` variable and simply not free it in the unload handler. Let us look at why that does not work and what does.

Consider this sketch:

```c
static uint8_t *myblk_persistent_backing;  /* wishful thinking */
static size_t   myblk_persistent_size;
```

The intuition is that if we allocate `myblk_persistent_backing` on first attach and refuse to free it on detach, a subsequent `kldload` will see the pointer still set and reuse the buffer. The problem is that this picture ignores how a KLD is actually loaded and unloaded. When `kldunload` removes our module, the kernel reclaims the module's text, data, and `.bss` segments along with the rest of its image. Our static pointer does not persist in some stable location; it vanishes together with the module. When `kldload` then brings the module back, the kernel allocates a fresh `.bss`, zeroes it, and our pointer starts life as `NULL` again. The `malloc`'d buffer we allocated on the previous attach is still sitting in kernel heap somewhere, but we have lost every handle to it. We have leaked it.

`SYSUNINIT` does not help either, because in a KLD context it fires on `kldunload`, not on some later "final tear-down" event. Registering a `SYSUNINIT` to free the buffer would free it on every unload, which is exactly what we did not want. There is no KLD-level hook that means "the module file is really, truly being removed from memory for good" distinct from plain `kldunload`.

Two techniques actually achieve cross-unload persistence, and both are used by `md(4)` in production. The first is a **file-backed store**. Instead of allocating a kernel heap buffer, the driver opens a file on an existing filesystem using the vnode I/O API (`VOP_READ`, `VOP_WRITE`, and the vnode reference taken via `vn_open`) and services BIOs by reading from and writing to that file. On unload, the driver closes the file; on the next load, it re-opens it. The persistence is real because it lives in a filesystem whose state is independent of our module. This is exactly what `md -t vnode -f /path/to/image.img` does, and you can study it in `/usr/src/sys/dev/md/md.c`.

The second technique is a **swap-backed store**. The driver allocates a VM object of type `OBJT_SWAP`, as we mentioned earlier, and maps pages from it on demand. The pager lives at a higher level of the kernel than our module, so the object can outlive any particular `kldunload` as long as something else holds a reference to it. In practice, `md(4)` uses this for swap-backed memory disks, and it ties the object's lifetime to a kernel-wide list rather than to a module instance.

For our teaching driver, we will not implement either technique. The point of showing this discussion is to make sure you understand why the apparent shortcut does not work, so that you do not spend an afternoon debugging a buffer that keeps disappearing after `kldunload`. If you want to experiment with real cross-unload persistence, read `md.c` carefully, particularly the `MD_VNODE` and `MD_SWAP` branches in `mdstart_vnode` and `mdstart_swap`, and note how the backing objects are attached to the per-unit `struct md_s` rather than to module-scope globals. That structural choice is what makes those backends work across module lifecycles.

### Sketching a Vnode-Backed Strategy Function

To make the earlier discussion concrete, let us sketch what a vnode-backed strategy function looks like at the code level. We are not going to drop this into our teaching driver. We are showing it so that you can see what the "real" solution involves and can recognise the same shape in `md.c` when you read it.

The idea is that the per-unit softc holds a reference to a vnode, acquired at attach time from a path provided by the administrator. The strategy function translates each BIO into a `vn_rdwr` call at the right offset and completes the BIO based on the result.

Attach acquires the vnode:

```c
static int
myblk_vnode_attach(struct myblk_softc *sc, const char *path)
{
    struct nameidata nd;
    int flags, error;

    flags = FREAD | FWRITE;
    NDINIT(&nd, LOOKUP, FOLLOW, UIO_SYSSPACE, path);
    error = vn_open(&nd, &flags, 0, NULL);
    if (error != 0)
        return (error);
    NDFREE_PNBUF(&nd);
    VOP_UNLOCK(nd.ni_vp);
    sc->vp = nd.ni_vp;
    sc->vp_cred = curthread->td_ucred;
    crhold(sc->vp_cred);
    return (0);
}
```

`vn_open` looks up the path and returns a locked, referenced vnode. We then drop the lock, because we want to hold a reference without blocking other operations, and we hang the vnode pointer on our softc. We also keep a reference to the credentials we will use for subsequent I/O.

The strategy function services BIOs against the vnode:

```c
static void
myblk_vnode_strategy(struct bio *bp)
{
    struct myblk_softc *sc = bp->bio_disk->d_drv1;
    int error;

    switch (bp->bio_cmd) {
    case BIO_READ:
        error = vn_rdwr(UIO_READ, sc->vp, bp->bio_data,
            bp->bio_length, bp->bio_offset, UIO_SYSSPACE,
            IO_DIRECT, sc->vp_cred, NOCRED, NULL, curthread);
        break;
    case BIO_WRITE:
        error = vn_rdwr(UIO_WRITE, sc->vp, bp->bio_data,
            bp->bio_length, bp->bio_offset, UIO_SYSSPACE,
            IO_DIRECT | IO_SYNC, sc->vp_cred, NOCRED, NULL,
            curthread);
        break;
    case BIO_FLUSH:
        error = VOP_FSYNC(sc->vp, MNT_WAIT, curthread);
        break;
    case BIO_DELETE:
        /* Vnode-backed devices usually do not support punching
         * holes through BIO_DELETE without additional plumbing.
         */
        error = EOPNOTSUPP;
        break;
    default:
        error = EOPNOTSUPP;
        break;
    }

    if (error != 0) {
        bp->bio_error = error;
        bp->bio_flags |= BIO_ERROR;
        bp->bio_resid = bp->bio_bcount;
    } else {
        bp->bio_resid = 0;
    }
    biodone(bp);
}
```

Notice how the shape of the switch is identical to our RAM-backed strategy function. The only difference is what the case arms do: instead of `memcpy` into a buffer, we call `vn_rdwr` against a vnode. The framework above us, GEOM and the buffer cache, does not know or care which backend we chose.

Detach releases the vnode:

```c
static void
myblk_vnode_detach(struct myblk_softc *sc)
{

    if (sc->vp != NULL) {
        (void)vn_close(sc->vp, FREAD | FWRITE, sc->vp_cred,
            curthread);
        sc->vp = NULL;
    }
    if (sc->vp_cred != NULL) {
        crfree(sc->vp_cred);
        sc->vp_cred = NULL;
    }
}
```

`vn_close` releases the vnode reference and, if this was the last reference, allows the vnode to be recycled. The credentials are reference-counted the same way.

Why does this give us cross-unload persistence? Because the state we care about, namely the contents of the backing store, lives in a file on a real filesystem whose lifetime is completely independent of our module. When we call `kldunload`, the vnode reference is released and the file closes; its contents on disk are preserved by the filesystem. When we call `kldload` again and attach, we open the file again and pick up where we left off.

The remaining subtleties are substantial. Error paths need to release the vnode if `vn_open` succeeded but subsequent registration steps failed. Calls to `vn_rdwr` can sleep, which means the strategy function must not be called from a context that disallows sleeping; in practice, that is why `md(4)` uses a dedicated worker thread for vnode-backed units. Reading a file can race with the administrator modifying it, so production drivers usually take measures to detect concurrent external changes. `VOP_FSYNC` is not free, so a fast path that batches writes before flushing is typical. And the vnode lifetime itself is bound by VFS's own reference counting, which interacts with unmount of the containing filesystem.

We will not add this to our teaching driver, but when you read `mdstart_vnode` in `/usr/src/sys/dev/md/md.c`, you will recognise every one of these issues handled carefully and explicitly.

### Wrapping Up Section 7

Persistence is a layered concept. Durability, consistency, and crash safety are all part of what a real storage device must provide, and different backing stores give different subsets of those guarantees. For a teaching driver, a `malloc`'d in-memory buffer is a reasonable choice, and we can add "survives module reload" semantics without much code by detaching the buffer from the per-instance softc.

For production, the techniques grow more elaborate: page-at-a-time allocation, swap-backed VM objects, vnode-backed files, dedicated worker threads, BIO_FLUSH coordination, and careful handling of every error path. `md(4)` is the canonical example in the FreeBSD tree, and reading it is strongly recommended.

In the next section, we will focus on the teardown path in detail. We will look at how GEOM coordinates unmount, detach, and cleanup; how access counts gate the module unload path; and how our driver should behave when something goes wrong mid-teardown. Storage unmount bugs are some of the nastier kinds of kernel bug, and careful attention here pays off for the rest of your driver-writing career.

## Section 8: Safe Unmount and Cleanup

Storage drivers handle the end of their lives with more care than character drivers because the stakes are higher. When a character driver unloads cleanly, the worst thing that can happen is that an open session is torn down, possibly with some bytes in flight being lost. When a storage driver unloads while a filesystem is mounted on it, the worst thing that can happen is that the kernel panics on the next BIO, and the user is left with a filesystem image that may or may not have been in a consistent state when the driver disappeared.

The good news is that the kernel's defences make the catastrophic case almost impossible if you use `g_disk` correctly. The refusal of `kldunload` to proceed when the GEOM access count is non-zero, which we saw in Section 6, is the primary safety net. But it is not the only concern. This section walks through the teardown path in detail so that you know what to expect, what to implement, and what to test.

### The Expected Teardown Sequence

The nominal sequence of events when a user wants to remove a storage driver is as follows.

1. The user unmounts every filesystem that is mounted on the device.
2. The user closes any program that has `/dev/myblk0` open for raw access.
3. The user calls `kldunload`.
4. The module unload function calls `disk_destroy`.
5. `disk_destroy` queues the provider for withering, which runs on the GEOM event thread.
6. The withering process waits for any in-flight BIO to complete.
7. The provider is removed from the GEOM topology and the `/dev` node is destroyed.
8. `disk_destroy` returns control to our unload function.
9. Our unload function frees the softc and the backing store.
10. The kernel unloads the module.

Each step has its own failure modes. Let us walk through them.

### Step 1: Unmount

The user runs `umount /mnt/myblk`. VFS asks UFS to flush the filesystem, which causes the buffer cache to issue any pending writes to GEOM, which routes them to our driver. Our strategy function services the writes and calls `biodone`. The buffer cache reports success; UFS disposes of its in-memory state; VFS releases the mount point. The consumer that UFS had attached to our provider is detached. The access count drops.

Our driver does not do anything special during this phase. We keep handling BIOs as they arrive until UFS stops issuing them.

### Step 2: Close Raw Access

The user ensures that no program holds `/dev/myblk0` open. If a `dd` is running, kill it. If a shell has the device open via `exec`, close that. Until every open handle is released, the access count will remain non-zero on at least one of the `r`, `w`, or `e` counters.

Again, our driver does nothing special. The `close(2)` calls on `/dev/myblk0` propagate through `devfs`, through GEOM's character-device integration, and release their access. No BIOs are issued for close.

### Step 3: kldunload

The user runs `kldunload myblk`. The kernel's module subsystem calls our unload function with `MOD_UNLOAD`. Our unload function calls `myblk_detach_unit`, which calls `disk_destroy`.

At this point, our driver is about to stop existing. We must not be holding any lock that could block, we must not be blocking on our own worker threads (we do not have any in this design), and we must not be issuing new BIOs. Nothing we do now should cause new work for the kernel.

### Step 4: disk_destroy

`disk_destroy` is the point of no return. Reading the source in `/usr/src/sys/geom/geom_disk.c` reveals that it does three things:

1. It sets a flag on the disk to indicate that destruction is in progress.
2. It queues a GEOM event that will actually dismantle the provider.
3. It waits for the event to complete.

While we are waiting, the GEOM event thread picks up the event and walks our geom. If the access counts are zero, the event proceeds. If they are not zero, the event panics with a message about trying to destroy a disk that still has users.

This is where the importance of Step 1 and Step 2 shows up. If you skip them and try to unload while the filesystem is mounted, the panic happens here. Fortunately, `g_disk` refuses to reach the panic because the module subsystem has already refused the unload earlier, but if you were to bypass the module subsystem and call `disk_destroy` directly from some other context, this is the check that protects the kernel.

### Step 5 to 7: Withering

The GEOM withering process is how providers are removed from the topology. It works by marking the provider as withered, cancelling any BIOs that were queued but not yet delivered, waiting for any in-flight BIOs to complete, removing the provider from the geom's provider list, and then removing the geom from the class. The `/dev` node is removed as part of this.

During withering, the strategy function may still be called for BIOs that were in flight before the withering started. Our strategy function will handle them normally, because our driver does not know or care that withering is in progress. The framework is responsible for ensuring no new BIOs are issued after the point of no return.

If our driver had worker threads, a queue, or other internal state, we would need to coordinate with withering carefully. `md(4)` is a good example of a driver that does this: its worker thread watches for a shutdown flag and drains its queue before exiting. Since our driver is entirely synchronous and single-threaded, we do not have this complication.

### Step 8 to 9: Free Resources

Once `disk_destroy` returns, the disk is gone, the provider is gone, and no more BIOs will arrive. It is safe to free the backing store and destroy the mutex.

```c
static void
myblk_detach_unit(struct myblk_softc *sc)
{

    if (sc->disk != NULL) {
        disk_destroy(sc->disk);
        sc->disk = NULL;
    }
    if (sc->backing != NULL) {
        free(sc->backing, M_MYBLK);
        sc->backing = NULL;
        sc->backing_size = 0;
    }
}
```

Our unload function then destroys the mutex and frees the softc.

```c
case MOD_UNLOAD:
    sc = myblk_unit0;
    if (sc == NULL)
        return (0);
    myblk_detach_unit(sc);
    mtx_destroy(&sc->lock);
    free(sc, M_MYBLK);
    myblk_unit0 = NULL;
    printf("myblk: unloaded\n");
    return (0);
```

### Step 10: Module Unload

The module subsystem unloads the `.ko` file. At this point, the driver is gone. Any attempt to reference the module by name will fail until the user loads it again.

### What Can Go Wrong

The happy path is smooth. Let us enumerate the unhappy paths and how to recognise them.

**`kldunload` returns `Device busy`**. The filesystem is still mounted, or a program still has the raw device open. Unmount and close, then retry. This is the most common failure, and it is benign.

**`disk_destroy` never returns**. Something is holding a BIO that will never complete, and the withering process is waiting for it. In practice, this happens if your strategy function fails to call `biodone` on some path. Look at `procstat -kk` of the `g_event` thread; if it is stuck in `g_waitfor_event`, you have a leaked BIO. The fix is in your strategy function: ensure that every path calls `biodone` exactly once.

**The kernel panics with "g_disk: destroy with open count"**. Your driver called `disk_destroy` while the provider still had users. This should not happen if you only call `disk_destroy` from the module unload path, because the module subsystem refuses to unload busy modules. But if you call `disk_destroy` in response to some other event, you must check the access count yourself or tolerate the panic.

**The kernel panics with "Freeing free memory"**. Your driver tried to free the softc or the backing store twice. Check your detach path for race conditions or for early exits that free and then fall through to free again.

**The kernel panics with "Page fault in kernel mode"**. Something is dereferencing a freed pointer, most often the backing store after it has been freed while a BIO is still in flight. The fix is to ensure `disk_destroy` completes before freeing anything the strategy function touches.

### The d_gone Callback

There is one more piece of the teardown story worth discussing. The `d_gone` callback is invoked when something other than our driver decides the disk should go away. The canonical example is hotplug removal: a user yanks a USB drive, the USB stack tells the storage driver the device is gone, and the storage driver wants to tell GEOM to tear down the disk as gracefully as possible even though I/O will start failing.

Our driver is a pseudo device; it does not have a physical disappearance event. But registering a `d_gone` callback costs nothing and makes the driver slightly more robust against future extensions.

```c
static void
myblk_disk_gone(struct disk *dp)
{

    printf("myblk: disk_gone(%s%u)\n", dp->d_name, dp->d_unit);
}
```

Register it with `sc->disk->d_gone = myblk_disk_gone;` before `disk_create`. The function is called by `g_disk` when `disk_gone` is invoked. You can trigger it manually during development by calling `disk_gone(sc->disk)` from a test path; you will not usually call it yourself in a pseudo driver.

Note the difference between `disk_gone` and `disk_destroy`. `disk_gone` says "this disk has physically vanished; stop accepting I/O and mark the provider as error-returning". `disk_destroy` says "remove this disk from the topology and free its resources". In a hot-unplug path, `disk_gone` is usually called first (by the bus driver, when it notices the device is gone), and `disk_destroy` is called later (by the module unload, or by the bus driver's detach function). Between the two calls, the disk still exists in the topology but all I/O fails. Our driver does not implement this dual-phase teardown; a USB mass storage driver, for instance, must.

### Testing the Teardown

Teardown bugs are often discovered not by careful testing but by accident, months later, when some user finds an unusual sequence that triggers them. It is much cheaper to test teardown deliberately.

Here are the tests I recommend running on any new storage driver.

**Basic unload**. Load, format, mount, unmount, unload. Verify `dmesg` shows our load and unload messages and nothing else. Repeat ten times to catch slow leaks.

**Unload without unmount**. Load, format, mount. Attempt to unload. Verify the unload is refused. Unmount, then unload. Verify no lingering state.

**Unload under load**. Load, format, mount, start a `dd if=/dev/urandom of=/mnt/myblk/stress bs=1m count=64`. While the `dd` runs, attempt to unload. Verify the unload is refused. Wait for `dd` to finish. Unmount. Unload. Verify clean.

**Unload with raw open**. Load. In another terminal, run `cat > /dev/myblk0` to hold the device open. Attempt to unload. Verify the unload is refused. Kill the cat. Unload. Verify clean.

**Reload stress**. Load, unload, load, unload in a tight loop for a minute. If `vmstat -m` or `zpool list` starts showing leaks, investigate.

**Panic on corruption**. This one is harder: deliberately corrupt the module state via a kernel debugger hook and verify that the driver does not silently return bad data. In practice, few beginners do this, and it is not required for a teaching driver.

If all of these pass, you have a reasonably robust teardown. Continue testing every change that touches the unload path.

### The Idempotency Principle

A good teardown path is idempotent: calling it twice is no worse than calling it once. This matters because error paths during attach may call the teardown before everything has been set up.

Write your teardown to check whether each resource was actually allocated before trying to free it.

```c
static void
myblk_detach_unit(struct myblk_softc *sc)
{

    if (sc == NULL)
        return;

    if (sc->disk != NULL) {
        disk_destroy(sc->disk);
        sc->disk = NULL;
    }
    if (sc->backing != NULL) {
        free(sc->backing, M_MYBLK);
        sc->backing = NULL;
        sc->backing_size = 0;
    }
}
```

Setting pointers to `NULL` after freeing them is a small discipline that pays off. It makes double-free mistakes obvious at runtime (they become no-ops rather than corruptions), and it makes the teardown function idempotent.

### Ordering and Reverse Order

A general teardown guideline: free resources in the reverse order of allocation. If attach goes `A -> B -> C`, detach should go `C -> B -> A`.

In our driver, attach goes `malloc backing -> disk_alloc -> disk_create`. So detach goes `disk_destroy -> free backing`. We skip freeing the disk because `disk_destroy` frees it for us.

This pattern is universal. Every well-written teardown function reverses the allocation order. When you see a detach that runs in the same order as the attach, suspect a bug.

### The MOD_QUIESCE Event

There is a third module event we have not mentioned: `MOD_QUIESCE`. It is delivered before `MOD_UNLOAD` and gives the module a chance to refuse unload if the driver is in a state where unloading is unsafe.

For most drivers, the GEOM access-count check is sufficient, and implementing `MOD_QUIESCE` is not needed. But if your driver has internal state that makes unload unsafe independent of GEOM (for example, a cache that must be flushed), `MOD_QUIESCE` is where you decline the unload by returning an error.

Our driver does not implement `MOD_QUIESCE`. The default behaviour is to accept it silently, which is the right thing for us.

### Coordinating With Future Worker Threads

If you ever add a worker thread to the driver, the teardown contract changes. You must:

1. Signal the worker to stop, typically by setting a flag on the softc.
2. Wake the worker if it is sleeping, typically with `wakeup` or `cv_signal`.
3. Wait for the worker to exit, typically with a `kthread_exit`-visible termination flag.
4. Only then call `disk_destroy`.
5. Free the softc and backing store.

Skipping any of these steps is a recipe for a panic. The usual failure mode is that the worker thread is sleeping inside a function that touches softc state after the softc has been freed. `md(4)` handles this carefully, and it is worth reading its worker shutdown code if you plan to add a worker to your own driver.

### Cleanup in the Face of Errors

One last concern: what happens if attach fails partway through? Suppose `disk_alloc` succeeds, but `disk_create` fails. Or suppose we add code that validates the sector size and rejects invalid configurations before calling `disk_create`.

The pattern for handling this is "single cleanup path". Write the attach function so that any failure jumps to a cleanup label that unwinds everything allocated so far, in reverse order.

```c
static int
myblk_attach_unit(struct myblk_softc *sc)
{
    int error;

    sc->backing_size = MYBLK_MEDIASIZE;
    sc->backing = malloc(sc->backing_size, M_MYBLK, M_WAITOK | M_ZERO);

    sc->disk = disk_alloc();
    sc->disk->d_name       = MYBLK_NAME;
    sc->disk->d_unit       = sc->unit;
    sc->disk->d_strategy   = myblk_strategy;
    sc->disk->d_ioctl      = myblk_ioctl;
    sc->disk->d_getattr    = myblk_getattr;
    sc->disk->d_gone       = myblk_disk_gone;
    sc->disk->d_sectorsize = MYBLK_SECTOR;
    sc->disk->d_mediasize  = MYBLK_MEDIASIZE;
    sc->disk->d_maxsize    = MAXPHYS;
    sc->disk->d_drv1       = sc;

    error = 0;  /* disk_create is void; no error path from it */
    disk_create(sc->disk, DISK_VERSION);
    return (error);

    /*
     * Future expansion: if we add a step that can fail between
     * disk_alloc and disk_create, use a cleanup label here.
     */
}
```

For our driver, `disk_alloc` does not fail in practice (it uses `M_WAITOK`), and `disk_create` is a `void` function that queues the real work asynchronously. So the attach path cannot really fail. But the pattern of preparing a single cleanup label is worth keeping in mind for drivers that grow more complex.

### Wrapping Up Section 8

Safe unmount and cleanup for a storage driver comes down to a small set of disciplines: handle every BIO through to `biodone`, never hold locks during completion callbacks, only call `disk_destroy` when the provider has no users, free resources in the reverse order of allocation, and test the teardown under load. The `g_disk` framework handles most of the hard parts; your job is to avoid breaking its invariants.

In the next section, we will step back from the teardown specifics and talk about how to let a storage driver grow. We will discuss refactoring, versioning, how to support multiple units cleanly, and what to do when the driver becomes more than a single source file. These are the habits that turn a teaching driver into something you can keep evolving for a long time.

## Section 9: Refactoring and Versioning

Our driver fits in a single file and solves one problem: it exposes a single pseudo disk of a fixed size, backed by RAM. That is a useful teaching starting point, but it is not where most real drivers live. A real storage driver evolves. It grows ioctl support. It grows multi-unit support. It grows tunable parameters. It splits into multiple source files. Its on-disk representation, if any, goes through format changes. It accumulates a history of compatibility choices.

This section is about the habits that let a driver grow gracefully. We will not add massive new features here; the companion labs and challenges will do that. What we will do is survey the refactoring and versioning questions that arise as any storage driver matures, and we will point to the FreeBSD-idiomatic answers for each.

### Multi-Unit Support

Right now our driver supports exactly one instance, hardcoded as `myblk0`. If you wanted two or three pseudo disks, the current code would need duplicate softcs and duplicate disk registrations. Real drivers solve this with a data structure that can hold any number of units.

The idiomatic FreeBSD pattern is a global list protected by a lock. The softc is allocated per unit and linked into the list. A loader-time tunable or an ioctl-driven call decides when to create a new unit. The unit number is allocated from a `unrhdr` (unique number range) allocator.

A sketch:

```c
static struct mtx          myblk_list_lock;
static LIST_HEAD(, myblk_softc) myblk_list =
    LIST_HEAD_INITIALIZER(myblk_list);
static struct unrhdr      *myblk_unit_pool;

static int
myblk_create_unit(size_t mediasize, struct myblk_softc **scp)
{
    struct myblk_softc *sc;
    int unit;

    unit = alloc_unr(myblk_unit_pool);
    if (unit < 0)
        return (ENOMEM);

    sc = malloc(sizeof(*sc), M_MYBLK, M_WAITOK | M_ZERO);
    mtx_init(&sc->lock, "myblk unit", NULL, MTX_DEF);
    sc->unit = unit;
    sc->backing_size = mediasize;
    sc->backing = malloc(mediasize, M_MYBLK, M_WAITOK | M_ZERO);

    sc->disk = disk_alloc();
    sc->disk->d_name       = MYBLK_NAME;
    sc->disk->d_unit       = sc->unit;
    sc->disk->d_strategy   = myblk_strategy;
    sc->disk->d_sectorsize = MYBLK_SECTOR;
    sc->disk->d_mediasize  = mediasize;
    sc->disk->d_maxsize    = MAXPHYS;
    sc->disk->d_drv1       = sc;
    disk_create(sc->disk, DISK_VERSION);

    mtx_lock(&myblk_list_lock);
    LIST_INSERT_HEAD(&myblk_list, sc, link);
    mtx_unlock(&myblk_list_lock);

    *scp = sc;
    return (0);
}

static void
myblk_destroy_unit(struct myblk_softc *sc)
{

    mtx_lock(&myblk_list_lock);
    LIST_REMOVE(sc, link);
    mtx_unlock(&myblk_list_lock);

    disk_destroy(sc->disk);
    free(sc->backing, M_MYBLK);
    mtx_destroy(&sc->lock);
    free_unr(myblk_unit_pool, sc->unit);
    free(sc, M_MYBLK);
}
```

The loader initialises the unit pool once, and then individual units can be created and destroyed independently. This is very close to the pattern `md(4)` uses.

We will not refactor our chapter driver to multi-unit yet, because the added code distracts from the other teaching goals. But you should know that this is where the driver would go. Supporting multiple units is one of the first extensions real drivers need.

### Ioctl Surface for Runtime Configuration

With multiple units comes the need to configure them at runtime. You do not want to compile a new module every time you want a second unit or a different size. The answer is an ioctl on a control device.

`md(4)` follows this pattern. There is a single `/dev/mdctl` device, and `mdconfig(8)` talks to it with ioctls. `MDIOCATTACH` creates a new unit with a specified size and backing type. `MDIOCDETACH` destroys a unit. `MDIOCQUERY` reads the state of a unit. `MDIOCRESIZE` changes the size.

For a driver of any sophistication, this is the right place to invest. Compile-time configuration via macros is fine for a toy. Runtime configuration via ioctls is what real administrators want.

If you were to add this to our driver, you would:

1. Create a `cdev` for the control device using `make_dev`.
2. Implement `d_ioctl` on the cdev, switching on a small set of ioctl numbers you define.
3. Write a user-space tool that issues the ioctls.

This is a substantial addition, which is why we mention it here without implementing it. Chapter 28 and later chapters will revisit this pattern.

### Splitting the Source File

At some point, a driver outgrows a single file. The usual decomposition for a FreeBSD storage driver is roughly:

- `driver_name.c`: the public module entry, ioctl dispatch, and attach/detach wiring.
- `driver_name_bio.c`: the strategy function and BIO path.
- `driver_name_backing.c`: the backing-store implementation.
- `driver_name_util.c`: small helpers, validation, and debug printing.
- `driver_name.h`: the shared header that declares the softc, enums, and function prototypes.

The Makefile is updated to list all of them in `SRCS`, and the build system handles the rest. This is the shape of `md(4)`, of `ata(4)`, and of most substantial drivers in the tree.

We will keep our driver in one file for the chapter. But when the challenges or your own extensions push it past, say, 500 lines, a decomposition like the above is the right move. Readers who want a concrete example should look at `/usr/src/sys/dev/ata/`, which splits a complex driver across many files along clean lines.

### Versioning

A storage driver has several kinds of versioning to care about.

**Module version**, declared with `MODULE_VERSION(myblk, 1)`. This is a monotonically increasing integer that other modules or userland tools can check. Bump it whenever you change the module's external behaviour in a way that cannot be detected from the code.

**Disk ABI version**, encoded in `DISK_VERSION`. This is the version of the `g_disk` interface that your driver was compiled against. If the kernel's `g_disk` changes incompatibly, it increments the version, and a driver compiled against the old version will fail to register. You do not set this directly; you pass the `DISK_VERSION` macro through `disk_create`, and it picks up whatever version the compile found in `geom_disk.h`. You should recompile drivers against the kernel you are targeting.

**On-disk format version**, for drivers that have any on-disk metadata. If your driver stamps a magic number and a version into a reserved sector, you must handle upgrades. Our driver has no on-disk format, so this does not apply yet, but it would if we added a proper backing-store header.

**Ioctl number version**. Once you define ioctls, their numbers are part of the userland ABI. Changing them breaks older userland tools. Use `_IO`, `_IOR`, `_IOW`, `_IOWR` with stable magic letters, and do not repurpose numbers.

For our chapter driver, the only version we care about right now is the module version. But keeping these four kinds of versioning in mind saves pain later.

### Debugging and Observability Helpers

As the driver grows, you will want to observe its state more richly than `dmesg` alone allows. Three tools are worth introducing now.

**`sysctl` nodes**. FreeBSD's `sysctl(3)` framework lets a module publish read-only or read-write variables that user-space tools can query. You create a tree under a chosen name and attach values to it. The pattern is standard; in roughly ten lines of code you can expose the number of BIOs serviced, the number of bytes read and written, and the current media size.

```c
SYSCTL_NODE(_dev, OID_AUTO, myblk, CTLFLAG_RD, 0,
    "myblk driver parameters");
static u_long myblk_reads = 0;
SYSCTL_ULONG(_dev_myblk, OID_AUTO, reads, CTLFLAG_RD, &myblk_reads,
    0, "Number of BIO_READ requests serviced");
```

**Devstat**. We are already using this through `g_disk`. It gives `iostat` and `gstat` their data. No further work needed.

**DTrace probes**. The `SDT` framework lets a module define static DTrace probes that incur zero cost when the probe is not being watched. These are especially useful in the BIO path because they let you see live request flow without recompiling.

```c
#include <sys/sdt.h>
SDT_PROVIDER_DECLARE(myblk);
SDT_PROBE_DEFINE3(myblk, , strategy, request,
    "int" /* cmd */, "off_t" /* offset */, "size_t" /* length */);

/* inside myblk_strategy: */
SDT_PROBE3(myblk, , strategy, request,
    bp->bio_cmd, bp->bio_offset, bp->bio_bcount);
```

You can then watch with `dtrace -n 'myblk::strategy:request {...}'`.

For the chapter driver we will not add all of this, but these are the patterns you should reach for as the driver grows.

### Naming Stability

One habit that is easy to overlook: do not rename things casually. The name `myblk` is in the device node, in the module version record, in the devstat name, possibly in sysctl nodes, in DTrace probes, and in documentation. Renaming it cascades through all of those. For a project driver, pick a name you can live with forever. `md`, `ada`, `nvd`, `zvol`, and other storage drivers have kept their names for years because renaming is an ABI-affecting change for user-space tooling.

### Keeping the Teaching Driver Simple

Everything in this section is a direction your driver might grow in. None of it is required for the teaching driver in this chapter. We are pointing at the directions so that you can recognise them when you see them in real driver source, and so that when you extend your own driver you do not have to invent these patterns from scratch.

The companion `myfirst_blk.c` remains a single file at the end of this chapter. Its README documents the extension points, and the challenge exercises add some of them. Beyond that, you are free to keep extending it, and every extension you make will use these patterns in some form.

### A Short Design Patterns Recap

At this point we have accumulated enough patterns that listing them helps. When you start your next storage driver, these are the patterns to reach for.

**The softc pattern.** One per-instance struct to hold everything the driver needs. Pointed at by `d_drv1`. Retrieved inside callbacks via `bp->bio_disk->d_drv1`.

**The attach/detach pair.** Attach allocates, initialises, and registers. Detach reverses the sequence. Both must be idempotent.

**The switch-and-biodone pattern.** Every strategy function switches on `bio_cmd`, services each command, sets `bio_resid`, and calls `biodone` exactly once.

**The defensive bounds check.** Validate offset and length against media size, using subtraction to avoid overflow.

**The coarse lock pattern.** A single mutex around the hot path is often enough for a teaching driver. Split it only when performance demands.

**The reverse-order teardown.** Free resources in the opposite order of allocation.

**The null-after-free pattern.** After freeing a pointer, set it to `NULL`. Catches double-frees.

**The single cleanup label.** In attach functions that can fail, all failures jump to a single cleanup label that unwinds state so far.

**The versioned ABI.** Pass `DISK_VERSION` to `disk_create`. Declare `MODULE_VERSION`. Use `MODULE_DEPEND` on every kernel module you rely on.

**The deferred-work pattern.** Work that must block (like vnode I/O) belongs in a worker thread, not in `d_strategy`.

**The observability-first habit.** Add `printf`, `sysctl`, or DTrace probes as you build. Observability retrofitted late is harder than observability designed in.

These are not exhaustive, but they are the patterns you will use most often. Each of them appears somewhere in our driver, and each of them appears throughout the real FreeBSD storage code.

### Wrapping Up Section 9

A maturing storage driver grows in predictable directions: multi-unit support, runtime configuration through ioctls, multiple source files, and stable versioning of every interface it exposes. None of this has to appear in the first version. Knowing where the growth will happen lets you make early choices that do not need to be undone later.

We have now covered every concept the chapter set out to teach. Before the hands-on labs, one more topic deserves a dedicated section, because it will repay you many times over as a driver author: observing a running storage driver. In the next section, we will look at the tools FreeBSD gives you for watching your driver in real time and for measuring its behaviour in a disciplined way.

## Section 10: Observability and Measuring Your Driver

Writing a storage driver is mostly a matter of getting the structure right. Once the structure is right, the driver just runs. But for the structure to stay right, you must be able to observe what is happening while the driver runs. You will want to know how many BIOs per second are hitting the strategy function, how long each one takes, how the latency distribution looks, how much memory the backing store consumes, whether any BIOs are getting retried, and whether any path is leaking completion.

FreeBSD gives you a remarkable set of tools for this, many of which we have already used casually. In this section we will walk through the most important ones in turn, with the goal of making you comfortable enough to reach for the right tool when the next strange symptom appears.

### gstat

`gstat` is the first tool to reach for. It updates a per-provider view of I/O activity in real time, and it shows you exactly what is happening at the GEOM layer.

```console
# gstat -I 1
dT: 1.002s  w: 1.000s
 L(q)  ops/s    r/s   kBps   ms/r    w/s   kBps   ms/w    %busy Name
    0    117      0      0    0.0    117    468    0.1    1.1| ada0
    0      0      0      0    0.0      0      0    0.0    0.0| myblk0
```

The columns, from left to right, are:

- `L(q)`: queue length. The number of BIOs currently outstanding on this provider.
- `ops/s`: total operations per second, regardless of direction.
- `r/s`: reads per second.
- `kBps` (for reads): read throughput in kilobytes per second.
- `ms/r`: average read latency, in milliseconds.
- `w/s`: writes per second.
- `kBps` (for writes): write throughput in kilobytes per second.
- `ms/w`: average write latency, in milliseconds.
- `%busy`: the percentage of time the provider was not idle.
- `Name`: the provider name.

For a driver you have just built, `gstat` tells you at a glance whether the kernel is sending traffic to your device and how your driver is performing relative to real disks. If the numbers look wildly different from what you expect, you have a starting point for investigation.

`gstat -p` shows only providers (the default). `gstat -c` shows only consumers, which is less useful for driver debugging. `gstat -f <regex>` filters by name. `gstat -b` batches the output one screen at a time instead of refreshing in place.

### iostat

`iostat` has a more traditional style but provides the same underlying data. It is useful when you want a text log rather than an interactive display.

```console
# iostat -x myblk0 1
                        extended device statistics
device     r/s     w/s    kr/s    kw/s  ms/r  ms/w  ms/o  ms/t qlen  %b
myblk0       0     128       0     512   0.0   0.1   0.0   0.1    0   2
myblk0       0     128       0     512   0.0   0.1   0.0   0.1    0   2
```

`iostat` can watch multiple devices at once and can be redirected to a log file for later analysis. For quick live views, `gstat` is usually better.

### diskinfo

`diskinfo` is less about live traffic and more about static properties. We have already used it to confirm our media size.

```console
# diskinfo -v /dev/myblk0
/dev/myblk0
        512             # sectorsize
        33554432        # mediasize in bytes (32M)
        65536           # mediasize in sectors
        0               # stripesize
        0               # stripeoffset
        myblk0          # Disk ident.
```

`diskinfo -c` runs a timing test, reading a few hundred megabytes and reporting the sustained rate. This is useful for a first-order performance comparison.

```console
# diskinfo -c /dev/myblk0
/dev/myblk0
        512             # sectorsize
        33554432        # mediasize in bytes (32M)
        65536           # mediasize in sectors
        0               # stripesize
        0               # stripeoffset
        myblk0          # Disk ident.

I/O command overhead:
        time to read 10MB block      0.000234 sec       =    0.000 msec/sector
        time to read 20480 sectors   0.001189 sec       =    0.000 msec/sector
        calculated command overhead                     =    0.000 msec/sector

Seek times:
        Full stroke:      250 iter in   0.000080 sec =    0.000 msec
        Half stroke:      250 iter in   0.000085 sec =    0.000 msec
        Quarter stroke:   500 iter in   0.000172 sec =    0.000 msec
        Short forward:    400 iter in   0.000136 sec =    0.000 msec
        Short backward:   400 iter in   0.000137 sec =    0.000 msec
        Seq outer:       2048 iter in   0.000706 sec =    0.000 msec
        Seq inner:       2048 iter in   0.000701 sec =    0.000 msec

Transfer rates:
        outside:       102400 kbytes in  0.017823 sec =  5746 MB/sec
        inside:        102400 kbytes in  0.017684 sec =  5791 MB/sec
```

These numbers are unusually fast because the backing store is RAM. On a real disk they would look very different, and comparing the numbers across devices is often the first diagnosis step for performance problems.

### sysctl

`sysctl` is how the kernel exposes its internal variables to user space. Many subsystems publish data through `sysctl`. You can browse the storage-related sysctls with:

```console
# sysctl -a | grep -i kern.geom
# sysctl -a | grep -i vfs
```

Adding your own sysctl tree to your driver, as we discussed in Section 9, lets you expose whatever metrics your driver needs to track, without the ceremony of defining a new tool.

### vmstat

`vmstat -m` shows memory allocation by `MALLOC_DEFINE` tag. Our driver uses `M_MYBLK`, so we can see how much memory our driver has allocated.

```console
# vmstat -m | grep myblk
       myblk     1  32768K         -       12  32K,32M
```

The columns are type, number of allocations, current size, protection requests, total requests, and possible sizes. For a driver that holds a 32 MiB backing store, the current size of 32 MiB is exactly what we expect. If it grew over time without an equivalent decrease on unload, we would have a leak.

`vmstat -z` shows zone allocator statistics. Much storage-related state lives in zones (GEOM providers, BIOs, disk structures), and `vmstat -z` is where to look if you suspect GEOM-level leaks.

### procstat

`procstat` shows per-thread kernel stacks. It is indispensable when something is stuck.

```console
# procstat -kk -t $(pgrep -x g_event)
  PID    TID COMM                TDNAME              KSTACK                       
    4 100038 geom                -                   mi_switch sleepq_switch ...
```

If the `g_event` thread is sleeping, the GEOM layer is idle. If it is stuck in a function with your driver's name on its stack, you have a BIO that is not completing.

```console
# procstat -kk $(pgrep -x kldload)
```

If `kldload` or `kldunload` is stuck, this shows you exactly where. Most often the culprit is a `disk_destroy` waiting for BIOs to drain.

### DTrace for the Block Layer

We introduced DTrace briefly in Section 6 and in Lab 7. Here let us go a little deeper, because DTrace is the single most effective tool for understanding live storage behaviour.

The Function Boundary Tracing (FBT) provider lets you place probes on the entry and return of nearly any kernel function. For our driver's strategy function, the probe name is `fbt::myblk_strategy:entry` for the entry and `fbt::myblk_strategy:return` for the return.

A simple one-liner that counts BIOs by command:

```console
# dtrace -n 'fbt::myblk_strategy:entry \
    { @c[args[0]->bio_cmd] = count(); }'
```

When you interrupt the script (with `Ctrl-C`), it prints a count per command value. `BIO_READ` is 1, `BIO_WRITE` is 2, `BIO_DELETE` is 3, `BIO_GETATTR` is 4, and `BIO_FLUSH` is 5. (The exact numbers are in `/usr/src/sys/sys/bio.h`.)

A latency histogram:

```console
# dtrace -n '
fbt::myblk_strategy:entry { self->t = timestamp; }
fbt::myblk_strategy:return /self->t/ {
    @lat = quantize(timestamp - self->t);
    self->t = 0;
}'
```

This gives you a log-scale histogram of how long each strategy-function execution took. For our in-memory driver, most buckets should be in the hundreds-of-nanoseconds range; anything in the millisecond range for an in-memory driver is suspicious.

A breakdown of I/O size:

```console
# dtrace -n 'fbt::myblk_strategy:entry \
    { @sz = quantize(args[0]->bio_bcount); }'
```

This shows you the distribution of BIO sizes. For a UFS-backed filesystem, you should see peaks at 4 KiB, 8 KiB, 16 KiB, and 32 KiB. For a raw `dd` with `bs=1m`, you should see a peak at 1 MiB (or the `MAXPHYS` cap, whichever is smaller).

DTrace is extraordinarily capable. The one-liners above barely scratch the surface. Two books to pick up, if you want to go deeper, are the original Sun "DTrace Guide" and Brendan Gregg's "DTrace Book". Both are older than FreeBSD 14.3 but the fundamentals still apply.

### kgdb and Crash Dumps

When your driver panics, FreeBSD can capture a crash dump. Configure the dump device in `/etc/rc.conf` (typically `dumpdev="AUTO"`) and verify with `dumpon`.

After a panic, reboot. `/var/crash/vmcore.last` (a symlink) points to the most recent dump. `kgdb /boot/kernel/kernel /var/crash/vmcore.last` opens the dump for inspection. Useful commands inside `kgdb`:

- `bt`: backtrace of the thread that panicked.
- `info threads`: list all threads in the crashed system.
- `thread N` then `bt`: backtrace of thread N.
- `print *var`: inspect a variable.
- `list function`: show source around a function.

If you have compiled your module with debugging symbols (the default for most kernel configurations), `kgdb` can show you source-level variables in your own code. This is a transformative capability once you get used to it.

### ktrace

`ktrace` is a user-space-oriented tool, but it can be useful for storage debugging when you want to see exactly what system calls a user program is making. If `newfs_ufs` is behaving oddly, you can trace it:

```console
# ktrace -f /tmp/newfs.ktr newfs_ufs /dev/myblk0
# kdump /tmp/newfs.ktr | head -n 50
```

The resulting trace shows the sequence of system calls, their arguments, and their results. For storage tools, this reveals exactly which ioctls are being issued and which file descriptors are being opened.

### dmesg and the Kernel Log

The humble `dmesg` is often the fastest way to diagnose a problem. Our driver prints to it on load and unload. The kernel prints to it on many other events, including GEOM class creation, access-count violations, and panics that the system recovers from.

Pro tip: redirect `dmesg -a` to a file at the start of each lab session. If something goes wrong you will have a complete log.

```console
# dmesg -a > /tmp/session.log
# # ... work ...
# dmesg -a > /tmp/session-final.log
# diff /tmp/session.log /tmp/session-final.log
```

This gives you a precise log of what the kernel reported during your session.

### A Simple Measurement Recipe

Here is a recipe you can use to produce a one-page performance profile of your driver.

1. Load the driver.
2. Run `diskinfo -c /dev/myblk0` and record the three transfer-rate numbers.
3. Format the device and mount it.
4. In one terminal, start `gstat -I 1 -f myblk0 -b` redirected to a file.
5. In another terminal, run `dd if=/dev/zero of=/mnt/myblk/stress bs=1m count=128`.
6. Stop `gstat` after `dd` completes and save the log.
7. Parse the log with `awk` to extract the peak ops/s, the peak throughput, and the average latency.
8. Unmount and unload.

This recipe scales. For a real driver you would automate it, run it on a matrix of block sizes, and plot the results. For a teaching driver, running it once or twice gives you a feeling for the numbers and a baseline to compare against after future changes.

### Comparing Against md(4)

One of the most useful exercises is to load `md(4)` in the same configuration as your driver and compare.

```console
# mdconfig -a -t malloc -s 32m
md0
# diskinfo -c /dev/md0
```

The numbers will likely be within a small factor of your driver's. If they are very different, something interesting is going on. The usual differences are:

- `md(4)` uses a worker thread that receives BIOs from the strategy function and processes them in a separate context. This adds a small amount of latency per BIO but allows higher concurrency.
- `md(4)` uses page-at-a-time backing, which is slightly slower per byte for sequential I/O but scales to much larger sizes.
- `md(4)` supports more BIO commands and attributes than our driver.

Comparing against `md(4)` is a form of debugging: if your driver is much slower or much faster than `md(4)` on the same workload, either you have done something unusual, or you have uncovered a difference worth understanding.

### Wrapping Up Section 10

Observability is not an afterthought. For a storage driver, it is how you keep your bearings. `gstat`, `iostat`, `diskinfo`, `sysctl`, `vmstat`, `procstat`, and DTrace are the tools you will reach for most often. `kgdb` and crash dumps are your backstop when things go catastrophically wrong.

Learn these tools now, while the driver is simple, because they will be the same tools you use when the driver is complex. A developer who can observe a running driver is much more effective than one who can only read source.

We have now covered every concept the chapter set out to teach, plus observability and measurement. Before we move on to the hands-on labs, let us spend some time reading real FreeBSD source. The case studies that follow anchor everything we have learned in code from the tree.

## Case Studies in Real FreeBSD Storage Code

Reading production driver source is the fastest way to internalise patterns. In this section we will walk through excerpts from three real drivers in `/usr/src/sys/`, with commentary that points out what each excerpt is doing and why. The excerpts are short on purpose; we will not read every line of each driver. We will pick the lines that matter.

Open the files alongside the text and follow along. The point is for you to see the same patterns in our driver reappearing in real drivers, under different names and with different constraints.

### Case Study 1: g_zero.c

`g_zero.c` is the simplest GEOM class in the tree. It is a read-always-zero, write-discard provider, with no real backing store and no real work to do. Its purpose is to give you a standard "null disk" you can test against. It is also an excellent teaching reference because it exercises the full `g_class` API in fewer than 150 lines.

Let us look at its strategy function, called `g_zero_start`.

```c
static void
g_zero_start(struct bio *bp)
{
    switch (bp->bio_cmd) {
    case BIO_READ:
        bzero(bp->bio_data, bp->bio_length);
        g_io_deliver(bp, 0);
        break;
    case BIO_WRITE:
        g_io_deliver(bp, 0);
        break;
    case BIO_GETATTR:
    default:
        g_io_deliver(bp, EOPNOTSUPP);
        break;
    }
}
```

Three behaviours, with `BIO_GETATTR` intentionally folded into the default case. Reads get zeroed. Writes are silently accepted. Anything else, including attribute queries, gets `EOPNOTSUPP`. The real `/usr/src/sys/geom/zero/g_zero.c` also handles `BIO_DELETE` in the successful-write path; our simplified excerpt above drops that case so you can see the shape clearly. Notice the call to `g_io_deliver` rather than `biodone`. That is because `g_zero` is a class-level GEOM module, not a `g_disk` module. `g_io_deliver` is the class-level completion call; `biodone` is the `g_disk` wrapper.

If you re-read our driver's strategy function side by side with this, you will see the same structure: a switch on `bio_cmd`, a case for each supported operation, a default error path. Our driver has more cases and it has a real backing store, but the shape is identical.

The `init` function that `g_zero` registers with the class is also small:

```c
static void
g_zero_init(struct g_class *mp)
{
    struct g_geom *gp;

    gp = g_new_geomf(mp, "gzero");
    gp->start = g_zero_start;
    gp->access = g_std_access;
    g_new_providerf(gp, "%s", gp->name);
    g_error_provider(g_provider_by_name(gp->name), 0);
}
```

When the `g_zero` module is loaded, this runs. It creates a new geom under the class, points the `start` method at the strategy function, uses the standard access handler, and creates a provider. That is everything it takes to expose `/dev/gzero`.

In our driver, `g_disk` does the equivalent of all this when `disk_create` is called. You can see here, once more, what `g_disk` is abstracting away. For most disk drivers that is a good trade; for `g_zero`, which does not want `g_disk`'s disk-specific features, using the class API directly is the better fit.

### Case Study 2: md.c, the Malloc Strategy Function

`md(4)` is a memory-disk driver with several backing types. The malloc backing type is the closest to our driver, and its strategy function is worth reading in detail.

Here is a simplified version of what happens when `md(4)`'s worker thread picks up a BIO for a `MD_MALLOC`-type disk. (In real `md(4)`, this is the function `mdstart_malloc`.)

```c
static int
mdstart_malloc(struct md_s *sc, struct bio *bp)
{
    u_char *dst, *src;
    off_t offset;
    size_t resid, len;
    int error;

    error = 0;
    resid = bp->bio_length;
    offset = bp->bio_offset;

    switch (bp->bio_cmd) {
    case BIO_READ:
        /* find the page that contains offset */
        /* copy len bytes out of it */
        /* advance, repeat until resid == 0 */
        break;
    case BIO_WRITE:
        /* find the page that contains offset */
        /* allocate it if not allocated yet */
        /* copy len bytes into it */
        /* advance, repeat until resid == 0 */
        break;
    case BIO_DELETE:
        /* free pages in the range */
        break;
    }

    bp->bio_resid = 0;
    return (error);
}
```

The key difference from our driver is the page-at-a-time backing. `md(4)` does not allocate one big buffer. It allocates 4 KiB pages on demand and indexes them through a data structure inside the softc. The benefit is that memory disks can be much larger than a single contiguous `malloc` would allow, and sparse regions (never written) consume no memory.

The cost is that every BIO may span multiple pages, so the strategy function has to loop. Each iteration copies `len` bytes into the current page, decrements `resid`, advances `offset`, and either exits when `resid` hits zero or moves on to the next page.

Our driver avoids this complexity at the cost of supporting only contiguous backing, which is fine up to a few tens of megabytes but not beyond.

If you wanted to extend our driver to match `md(4)`'s scale, the page-at-a-time pattern is the direction you would go. It is straightforward once you have `md(4)` in front of you as a reference.

### Case Study 3: md.c, the Module Load Path

Another piece of `md(4)` worth studying is how it bootstraps its class and sets up the control device.

```c
static void
g_md_init(struct g_class *mp __unused)
{
    /*
     * Populate sc_list with pre-loaded memory disks
     * (preloaded kernel images, ramdisks from boot, etc.)
     */
    /* ... */

    /*
     * Create the control device /dev/mdctl.
     */
    status = make_dev_p(MAKEDEV_CHECKNAME | MAKEDEV_WAITOK,
        &status_dev, &mdctl_cdevsw, 0, UID_ROOT, GID_WHEEL,
        0600, MDCTL_NAME);
    /* ... */
}
```

The `g_md_init` function runs once per kernel boot, when the `md(4)` class is first instantiated. It handles any memory disks that the loader preloaded into memory (so that the kernel can boot from a memory-disk root) and it creates the control device `/dev/mdctl` through which `mdconfig` will later talk to the driver.

Compare this to our loader, which is a simple `moduledata_t` that calls `disk_create` directly. `md(4)` does not create any memory disks by default. It only creates them in response to preload events or in response to `MDIOCATTACH` ioctls on the control device.

The pattern here is generalisable. If you want a storage driver that creates units on demand rather than at load time, you:

1. Register the class (or, for `g_disk`-based drivers, set up the infrastructure).
2. Create a control device with a cdevsw that supports ioctls.
3. Implement create, destroy, and query ioctls.
4. Write a user-space tool that talks to the control device.

`md(4)` is the canonical example. Other drivers, like `geli(4)` and `gmirror(4)`, use a slightly different pattern because they are GEOM transformation classes rather than disk drivers, but the overall shape is similar.

### Case Study 4: The newbus Side of a Real Storage Driver

For contrast, let us look briefly at how a real hardware-backed storage driver attaches. The `ada(4)` driver, for example, is a CAM-based ATA driver. Its attach path is not directly visible as a single function, because CAM mediates between the driver and the hardware, but the end of the chain looks like this (abbreviated from `/usr/src/sys/cam/ata/ata_da.c`):

```c
static void
adaregister(struct cam_periph *periph, void *arg)
{
    struct ada_softc *softc;
    /* ... */

    softc->disk = disk_alloc();
    softc->disk->d_open = adaopen;
    softc->disk->d_close = adaclose;
    softc->disk->d_strategy = adastrategy;
    softc->disk->d_getattr = adagetattr;
    softc->disk->d_dump = adadump;
    softc->disk->d_gone = adadiskgonecb;
    softc->disk->d_name = "ada";
    softc->disk->d_drv1 = periph;
    /* ... */
    softc->disk->d_unit = periph->unit_number;
    /* ... */
    softc->disk->d_sectorsize = softc->params.secsize;
    softc->disk->d_mediasize = ...;
    /* ... */

    disk_create(softc->disk, DISK_VERSION);
    /* ... */
}
```

The structure is identical to ours: fill in a `struct disk` and call `disk_create`. The differences are:

- `d_strategy` is `adastrategy`, which translates BIOs into ATA commands and issues them to the controller via CAM.
- `d_dump` is implemented, because `ada(4)` supports kernel crash dumps. Our driver does not implement this.
- The fields like `d_sectorsize` and `d_mediasize` come from hardware probing, not from macros.

From `g_disk`'s perspective, however, `ada0` and our `myblk0` are the same kind of thing. Both are disks. Both receive BIOs. Both are completed with `biodone`. The difference is in where the bytes actually go.

This is the uniformity that `g_disk` provides. Your driver can choose any backing technology, and as long as it fills in `struct disk` correctly, it looks like any other disk to the rest of the kernel.

### Takeaways from the Case Studies

Three patterns become clearer after reading these excerpts.

First, the strategy function is always a switch on `bio_cmd`. The cases vary, but the switch is always there. Memorise this pattern: incoming BIO -> switch -> case per command -> completion. It is the heart of every storage driver.

Second, `g_disk` drivers are structurally identical at the registration level. Whether the driver is a RAM disk or a real SATA drive, the registration code looks the same. The differences are in what happens when the BIO arrives.

Third, more sophisticated drivers enqueue work to a dedicated thread. Our driver does not, because it can do its work synchronously in any thread. Drivers that do slow or blocking work must enqueue, because strategy functions run in the caller's thread context.

With these patterns in mind, you can now read almost any storage driver in the FreeBSD tree and follow its overall structure, even if specific details about hardware or sub-frameworks require further study.

We have now covered every concept the chapter set out to teach, plus observability, measurement, and a few real case studies. In the next part of the chapter, we will put this knowledge to work through hands-on labs. The labs build on the driver you have been writing and on the skills you have been using, and they take you from the minimum working driver through persistence, mount, and cleanup scenarios. Let us begin.

## Hands-On Labs

Each lab is a self-contained checkpoint. They are designed to be done in order, but you can revisit any lab later if you want to practise a specific skill. Every lab has a companion folder under `examples/part-06/ch27-storage-vfs/`, which contains the reference implementation and the artifacts you would produce if you typed the code by hand.

Before you start, make sure you have the chapter's driver building cleanly against your local kernel. From a fresh checkout of the examples tree:

```console
# cd examples/part-06/ch27-storage-vfs
# make
# ls myblk.ko
myblk.ko
```

If that works, you are ready. If not, revisit the Makefile and the advice in Chapter 26, section "Your Build Environment".

### Lab 1: Explore GEOM on a Running System

**Goal.** Build comfort with the GEOM inspection tools before you touch any code.

**What you do.**

On your FreeBSD 14.3 system, run the following commands and take notes in your lab logbook.

```console
# geom disk list
# geom part show
# geom -t | head -n 40
# gstat -I 1
# diskinfo -v /dev/ada0   # or whatever your primary disk is called
```

**What you look for.**

Identify every `DISK` class geom. For each, note its provider name, its media size, its sector size, and its current mode. Notice which geoms have partitioning layers on top and which do not. If your system has `geli` or `zfs`, notice the chain of classes.

**Stretch question.** Which of your geoms have non-zero access counts right now? Which ones are free? What would happen if you tried to run `newfs_ufs` on each?

**Reference implementation.** `examples/part-06/ch27-storage-vfs/lab01-explore-geom/README.md` contains a suggested walkthrough and a sample output transcript from a typical system.

### Lab 2: Build the Skeleton Driver

**Goal.** Get the Section 3 skeleton driver compiling and loading on your system.

**What you do.**

Copy `examples/part-06/ch27-storage-vfs/lab02-skeleton/myfirst_blk.c` and its `Makefile` into a working directory. Build it.

```console
# cp -r examples/part-06/ch27-storage-vfs/lab02-skeleton /tmp/myblk
# cd /tmp/myblk
# make
```

Load the module.

```console
# kldload ./myblk.ko
# dmesg | tail -n 2
# ls /dev/myblk0
# geom disk list myblk0
```

Unload it.

```console
# kldunload myblk
# ls /dev/myblk0
```

**What you look for.**

Confirm that the kernel printed your `myblk: loaded` message. Confirm that `/dev/myblk0` appeared. Confirm that `geom disk list` reported the expected media size. Confirm that the node disappeared after unload.

**Stretch question.** What happens if you try `newfs_ufs -N /dev/myblk0` with the skeleton driver? Can you read the output? Why does the dry run succeed even though real writes would fail?

### Lab 3: Implement the BIO Handler

**Goal.** Add the working strategy function from Section 5 to the skeleton driver.

**What you do.**

Starting from the skeleton, implement `myblk_strategy` with support for `BIO_READ`, `BIO_WRITE`, `BIO_DELETE`, and `BIO_FLUSH`. Allocate the backing buffer in `myblk_attach_unit` and free it in `myblk_detach_unit`.

Build, load, and test.

```console
# dd if=/dev/zero of=/dev/myblk0 bs=4096 count=16
# dd if=/dev/myblk0 of=/dev/null bs=4096 count=16
# dd if=/dev/random of=/dev/myblk0 bs=4096 count=16
# dd if=/dev/myblk0 of=/tmp/a bs=4096 count=16
# dd if=/dev/myblk0 of=/tmp/b bs=4096 count=16
# cmp /tmp/a /tmp/b
```

**What you look for.**

The last `cmp` must succeed with no output. If it prints `differ: byte N`, your strategy function is racing or returning stale data.

**Stretch question.** Put a `printf` in the strategy function that reports `bio_cmd`, `bio_offset`, and `bio_bcount`. Run `dd if=/dev/myblk0 of=/dev/null bs=1m count=1` and look at `dmesg`. What size did `dd` actually issue? Do you see fragmentation?

**Reference implementation.** `examples/part-06/ch27-storage-vfs/lab03-bio-handler/myfirst_blk.c`.

### Lab 4: Increase Size and Mount UFS

**Goal.** Increase the backing store to 32 MiB and mount UFS on the device.

**What you do.**

Change `MYBLK_MEDIASIZE` to `(32 * 1024 * 1024)` and rebuild. Load the module. Format and mount.

```console
# newfs_ufs /dev/myblk0
# mkdir -p /mnt/myblk
# mount /dev/myblk0 /mnt/myblk
# echo "hello" > /mnt/myblk/greeting.txt
# cat /mnt/myblk/greeting.txt
# umount /mnt/myblk
# mount /dev/myblk0 /mnt/myblk
# cat /mnt/myblk/greeting.txt
# umount /mnt/myblk
# kldunload myblk
```

**What you look for.**

Verify that the file survives an unmount-and-remount. Verify that the access counts in `geom disk list` are zero after unmount. Verify that `kldunload` succeeds cleanly.

**Stretch question.** Watch `gstat -I 1` while running `dd if=/dev/zero of=/mnt/myblk/big bs=1m count=16`. Can you see the writes arrive in bursts? What size are the individual BIOs? Hint: UFS's default block size is typically 32 KiB on a filesystem this small.

**Reference implementation.** `examples/part-06/ch27-storage-vfs/lab04-mount-ufs/myfirst_blk.c`.

### Lab 5: Observing Real Cross-Reload Persistence with md(4)

**Goal.** Confirm experimentally that cross-reload persistence requires external backing, as Section 7 argued, by using `md(4)`'s vnode mode as a control.

**What you do.**

First, demonstrate that our RAM-backed `myblk` loses its filesystem on reload. Load, format, mount, write, unmount, unload, reload, mount again, and observe the empty filesystem.

```console
# kldload ./myblk.ko
# newfs_ufs /dev/myblk0
# mount /dev/myblk0 /mnt/myblk
# echo "not persistent" > /mnt/myblk/token.txt
# umount /mnt/myblk
# kldunload myblk
# kldload ./myblk.ko
# mount /dev/myblk0 /mnt/myblk
# ls /mnt/myblk
```

The `ls` should show an empty or fresh UFS directory; the `token.txt` is gone because the backing buffer was reclaimed by the kernel when the module unloaded.

Now do the same sequence with `md(4)`'s vnode backend, which uses a real file on disk:

```console
# truncate -s 64m /var/tmp/mdimage.img
# mdconfig -a -t vnode -f /var/tmp/mdimage.img -u 9
# newfs_ufs /dev/md9
# mount /dev/md9 /mnt/md
# echo "persistent" > /mnt/md/token.txt
# umount /mnt/md
# mdconfig -d -u 9
# mdconfig -a -t vnode -f /var/tmp/mdimage.img -u 9
# mount /dev/md9 /mnt/md
# cat /mnt/md/token.txt
persistent
```

**What you look for.**

The first sequence loses the file; the second preserves it. The difference is that `md9` is backed by a real file on disk, whose state survives regardless of what happens inside the kernel. Contrast this with `myblk0`, which is backed by kernel heap that disappears on `kldunload`.

**Stretch question.** Read the `MD_VNODE` branch of `mdstart_vnode` in `/usr/src/sys/dev/md/md.c`. Identify where the vnode reference is stored (hint: it lives on the per-unit `struct md_s`, not a module-scope global). Explain in your own words why that design is what lets the backing survive module lifecycles.

**Reference implementation.** `examples/part-06/ch27-storage-vfs/lab05-persistence/README.md` walks through both sequences and their diagnostic output.

### Lab 6: Safe Unmount Under Load

**Goal.** Verify that the teardown path handles an active filesystem correctly.

**What you do.**

Load the module, format it, mount it. In one terminal, start a stress loop.

```console
# while true; do dd if=/dev/urandom of=/mnt/myblk/stress bs=4k \
    count=512 2>/dev/null; sync; done
```

In another terminal, attempt to unload.

```console
# kldunload myblk
kldunload: can't unload file: Device busy
```

Stop the stress loop. Unmount. Unload.

**What you look for.**

The initial unload must fail gracefully. After unmount, the final unload must succeed. `dmesg` must not show any kernel warnings.

**Stretch question.** Instead of killing the stress loop, try `umount /mnt/myblk` directly. Does UFS let you unmount while writes are in flight? What is the error, and what does it mean?

**Reference implementation.** `examples/part-06/ch27-storage-vfs/lab06-safe-unmount/` includes a test script that performs the sequence above and reports failures.

### Lab 7: Observing BIO Traffic with DTrace

**Goal.** Use DTrace to see the BIO path as it happens.

**What you do.**

With the driver loaded and a filesystem mounted, run the following DTrace one-liner in one terminal:

```console
# dtrace -n 'fbt::myblk_strategy:entry { \
    printf("cmd=%d off=%lld len=%u", \
        args[0]->bio_cmd, args[0]->bio_offset, \
        args[0]->bio_bcount); \
    @count[args[0]->bio_cmd] = count(); \
}'
```

In another terminal, create and read files on the mounted filesystem.

**What you look for.**

Note which BIO commands you see and in what quantities. Note the typical offsets and lengths. Compare the patterns from `dd` traffic versus `cp` traffic versus `tar` traffic. Note how `cp` or `mv` might produce very different BIO patterns depending on what the buffer cache decides to flush.

**Stretch question.** Issue `sync` while the DTrace is running. What BIO commands does `sync` cause? What about `newfs_ufs`?

**Reference implementation.** `examples/part-06/ch27-storage-vfs/lab07-dtrace/README.md` with sample DTrace output and notes.

### Lab 8: Adding a getattr Attribute

**Goal.** Implement a `d_getattr` callback that responds to `GEOM::ident`.

**What you do.**

Add the `myblk_getattr` function from Section 5 to the driver, and register it on the disk before `disk_create`. Rebuild, reload, and check `diskinfo -v /dev/myblk0`.

**What you look for.**

The `ident` field should now show `MYBLK0` instead of `(null)`.

**Stretch question.** What other attributes might a filesystem query? Look at `/usr/src/sys/geom/geom.h` for named attributes like `GEOM::rotation_rate`. Try implementing that too.

**Reference implementation.** `examples/part-06/ch27-storage-vfs/lab08-getattr/myfirst_blk.c`.

### Lab 9: Exploring md(4) for Comparison

**Goal.** Read a real FreeBSD storage driver and identify the patterns we have used.

**What you do.**

Open `/usr/src/sys/dev/md/md.c`. It is a long file. Do not try to read every line. Instead, find and understand these specific things:

1. The `g_md_class` structure at the top of the file.
2. The `struct md_s` softc.
3. The `mdstart_malloc` function that handles BIO_READ and BIO_WRITE for `MD_MALLOC` memory disks.
4. The worker-thread pattern in `md_kthread` (or its equivalent in your version).
5. The `MDIOCATTACH` ioctl handler that creates new units on demand.

Compare each of these to the corresponding code in our driver.

**What you look for.**

Spot the differences. Where does `md(4)` have features we do not? Where does our driver have the same mechanism in simpler form? Where would you need to extend our driver to add one of `md(4)`'s features?

**Reference notes.** `examples/part-06/ch27-storage-vfs/lab09-md-comparison/NOTES.md` contains a mapped walkthrough of the relevant sections of `md.c` for FreeBSD 14.3.

### Lab 10: Break It On Purpose

**Goal.** Induce known failure modes so that you can recognise them quickly in real work.

**What you do.**

Take a clean copy of the completed driver from Lab 8. In separate copies (do not mix the breakages together), introduce the following bugs one at a time, rebuild, load, and observe.

**Breakage 1: Forget biodone.** Comment out the `biodone(bp)` call in the `BIO_READ` case. Load, mount, and run `cat` on a file. The `cat` will hang forever. Attempt to kill it with `Ctrl-C`; it may not respond. Use `procstat -kk` on the stuck PID to see where the process is waiting. This is the classic leaked-BIO symptom.

**Breakage 2: Free backing before disk_destroy.** In `myblk_detach_unit`, swap the order so that `free(sc->backing, ...)` comes before `disk_destroy(sc->disk)`. Load, format, mount, unmount, and try to unload. If no BIO is in flight during the unload window, you will escape unharmed. If any BIO is in flight (use a running `dd` to ensure this), you will panic with a page fault.

**Breakage 3: Skip bio_resid.** Remove the `bp->bio_resid = 0` line from the `BIO_READ` case. Load, format, mount, and create a file. Read it back. Depending on what garbage was in `bio_resid` at allocation time, the filesystem may report incorrect read sizes and may log errors. Sometimes it works; sometimes it does not. This is the characteristic intermittent failure of a forgotten `bio_resid`.

**Breakage 4: Off-by-one bounds.** Change the bounds check from `offset > sc->backing_size` to `offset >= sc->backing_size`. This rejects valid reads at the last offset. Load, format, mount. Try to write a file that extends to the very last block. Observe whether UFS notices; whether `dd` notices; what error is reported.

**What you look for.**

In each case, describe in your logbook what you observed, which tool revealed the problem (dmesg, `procstat`, `gstat`, panic trace), and what the fix would be. Then apply the fix and confirm normal operation.

**Stretch question.** What sequence of commands reliably reproduces each failure? Can you write a shell script that deterministically triggers Breakage 1 or Breakage 2?

**Reference notes.** `examples/part-06/ch27-storage-vfs/lab10-break-on-purpose/BREAKAGES.md` contains short descriptions and a test script for each failure mode.

### Lab 11: Measure Under Varying Block Sizes

**Goal.** Understand how BIO size affects throughput.

**What you do.**

With the driver loaded and a filesystem mounted, run `dd` with progressively larger block sizes and time each run.

```console
# for bs in 512 4096 32768 131072 524288 1048576; do
    rm -f /mnt/myblk/bench
    time dd if=/dev/zero of=/mnt/myblk/bench bs=$bs count=$((16*1024*1024/bs))
done
```

Record the throughput in each case.

**What you look for.**

Throughput should increase as block size increases, then plateau at or near `d_maxsize` (typically 128 KiB). Very small block sizes will be dominated by per-BIO overhead.

**Stretch question.** At what block size does the curve visibly plateau? Why?

### Lab 12: Race Test With Two Processes

**Goal.** Observe how the driver handles simultaneous access from multiple processes.

**What you do.**

With the driver loaded and a filesystem mounted, run two `dd` processes in parallel writing to different files.

```console
# dd if=/dev/urandom of=/mnt/myblk/a bs=4k count=1024 &
# dd if=/dev/urandom of=/mnt/myblk/b bs=4k count=1024 &
# wait
```

Record the combined throughput.

**What you look for.**

Both writes should complete without corruption. Verify with `md5` or `sha256` on each file. The combined throughput may be slightly less than twice the single-process throughput because of lock contention in our coarse mutex.

**Stretch question.** Does removing the mutex affect throughput? Does it cause corruption? Why or why not?

### A Word on Lab Discipline

Every lab is small, and none of them is an exam. If you get stuck, the reference implementation is there for you to compare against. Do not copy-paste it as your first attempt, though. The copying is not the skill. The skill is in typing, reading, diagnosing, and verifying.

Keep your logbook open. Record what you ran, what you saw, and what surprised you. Storage bugs often repeat across projects, and your future self will thank your current self for the notes.

## Challenge Exercises

The challenge exercises stretch the driver a little further. Each one is scoped to something a beginner can accomplish with the material already covered in the chapter, combined with a thoughtful reading of FreeBSD source. They are not timed. Take your time. Open the source tree. Consult manual pages. Compare your solution to `md(4)` when in doubt.

Every challenge below has a stub folder under `examples/part-06/ch27-storage-vfs/`, but no reference solution is provided. The point is to work through them yourself. Solutions are left as follow-ups that you can compare with peers or post in your study notes.

### Challenge 1: Expose a Read-Only Mode

Add a module-load tunable that lets the driver come up in read-only mode. In read-only mode, `BIO_WRITE` and `BIO_DELETE` should fail with `EROFS`. `newfs_ufs` should refuse to format the device, and `mount` without `-r` should refuse to mount it.

Hint. The tunable can be a `sysctl_int` bound to a static variable. `TUNABLE_INT` is another way, used at load time only. Your strategy function can check the variable before dispatching writes. Remember that changing the mode at runtime while a filesystem is mounted is a recipe for corruption; you can either disallow the change or document that the tunable only takes effect at module load.

### Challenge 2: Implement a Second Unit

Add support for exactly two units: `myblk0` and `myblk1`. Each should have its own backing store of its own size. Do not try to implement fully dynamic unit allocation; just hardcode two softcs and two attach calls in the module loader.

Hint. Move the backing allocation, disk allocation, and disk creation into `myblk_attach_unit` parameterised by unit number and size, and call it twice from the loader. Make sure the detach path walks both units.

### Challenge 3: Honour BIO_DELETE with a Sysctl Counter

Extend `BIO_DELETE` handling to also bump a `sysctl` counter that reports total bytes deleted. Verify with `sysctl dev.myblk` while running `fstrim /mnt/myblk` or while `dd` writes and overwrites files.

Hint. UFS by default does not issue `BIO_DELETE`. To see delete traffic, mount with `-o trim`. You can verify the trim flow with your DTrace one-liner from Lab 7.

### Challenge 4: Respond to BIO_GETATTR for rotation_rate

Extend `myblk_getattr` to answer `GEOM::rotation_rate` with `DISK_RR_NON_ROTATING` (defined in `/usr/src/sys/geom/geom_disk.h`). Verify with `gpart show` and `diskinfo -v` that the device reports as non-rotating.

Hint. The attribute is returned as a plain `u_int`. Look at how `md(4)` handles `BIO_GETATTR` for comparable attributes.

### Challenge 5: Resizing the Device

Add an ioctl that lets user space resize the backing store while nothing is mounted. If a filesystem is mounted, the ioctl must fail with `EBUSY`. If the resize succeeds, update `d_mediasize` and notify GEOM so that `diskinfo` reports the new size.

Hint. Look at `md(4)`'s `MDIOCRESIZE` handling for the pattern. This is a non-trivial challenge; take your time and test with throwaway filesystems. Do not attempt this on any backing that you would be sad to lose.

### Challenge 6: A Write Counter and Rate Display

Add per-second write-byte counters, exposed via `sysctl`, and a small user-space shell script that reads the sysctl every second and prints a human-readable rate. This is useful for testing and it gives you experience wiring metrics through the kernel's observability machinery.

Hint. Use `atomic_add_long` on the counters. The shell script is a one-liner in `while true` loops.

### Challenge 7: A Fixed Pattern Backing Store

Implement a backing-store mode where reads always return a fixed byte pattern and writes are silently discarded. This is similar to `g_zero` but with a configurable pattern byte. It is useful for stress-testing the layers above when you do not care about data content.

Hint. Branch on a mode variable inside the strategy function. Keep the in-memory backing for the normal mode and skip the `memcpy` in pattern mode.

### Challenge 8: Write a mdconfig-Like Control Utility

Write a small user-space program that talks to a control device on your driver (you will need to add one) and can create, destroy, and query units at runtime. The program should accept command-line flags similar to `mdconfig`.

Hint. This is a substantial challenge. Start with a single ioctl that prints "hello" and build up from there. `make_dev` on a cdev for your control device, then implement `d_ioctl` on that cdev.

### Challenge 9: Survive a Simulated Crash

Add a mode where the driver drops every Nth write silently (pretending the write succeeded but actually doing nothing). Use this to test UFS's resilience to lost writes.

Hint. This is a dangerous mode. Only run it on throwaway filesystems. You should be able to reproduce interesting `fsck_ffs` repair scenarios with it. Be ready to explain to yourself why this mode is only safe on pseudo devices you can regenerate from scratch.

### Challenge 10: Understand md(4) Well Enough to Teach It

Write a one-page explanation of how `md(4)` creates a new unit in response to `MDIOCATTACH`. Cover the ioctl path, the softc allocation, the backing-type-specific initialisation, the `g_disk` wiring, and the worker thread creation. This is a reading challenge rather than a coding challenge, but it is one of the most useful exercises you can do to deepen your grasp of the storage stack.

Hint. `/usr/src/sys/dev/md/md.c` and `/usr/src/sbin/mdconfig/mdconfig.c` are the two files to read. Pay attention to the `struct md_ioctl` structure in `/usr/src/sys/sys/mdioctl.h`, because that is the ABI between user space and the kernel.

### When to Attempt Challenges

You do not need to do all of them. Pick one or two that speak to something you are curious about or something you can imagine using later. A single carefully-done challenge is worth more than five half-done ones. The reference `md(4)` implementation will be there whenever you want to compare your approach to a production driver.

## Troubleshooting

Storage drivers have a particular family of failure modes. Some of them are obvious as they happen. Others are silent at first and become obvious only after reboot, sometimes with data corruption in between. This section lists the symptoms you are most likely to see while working through the chapter and the labs, along with the usual causes and fixes. Use it as a reference when things go wrong, and read it through at least once before you start, because it is much easier to recognise a failure mode the second time.

### `kldload` Succeeds but No /dev Node Appears

**Symptom.** `kldload` returns zero. `kldstat` shows the module loaded. But `/dev/myblk0` does not exist.

**Likely causes.**

- You forgot to call `disk_create`. The softc is allocated, the disk is allocated, but the disk is not registered with GEOM.
- You called `disk_create` with `d_name` set to a null pointer or an empty string.
- You called `disk_create` with `d_mediasize` set to zero. `g_disk` silently refuses to create a provider with zero size.
- You called `disk_create` before filling in the fields. The framework captures the field values at registration time and does not re-read them later.

**Fix.** Check the kernel message buffer with `dmesg`. `g_disk` prints a diagnostic when it rejects a registration. Fix the field value and rebuild.

### `kldload` Fails with "module version mismatch"

**Symptom.** Loading the module reports `kldload: can't load ./myblk.ko: No such file or directory` or a more explicit error about version mismatch.

**Likely causes.**

- You compiled against a different kernel than the one currently running.
- You changed `DISK_VERSION` on your own, which you should never do.
- You forgot `MODULE_VERSION(myblk, 1)`.

**Fix.** Check `uname -a` and the kernel version your build chose. Recompile against the running kernel.

### `diskinfo` Prints the Wrong Size

**Symptom.** `diskinfo -v /dev/myblk0` prints a size that does not match `MYBLK_MEDIASIZE`.

**Likely causes.**

- You set `d_mediasize` to the wrong expression. A common off-by-one is setting it to the sector count rather than the byte count.
- You have `MYBLK_MEDIASIZE` defined as something other than `(size * 1024 * 1024)` and the macro is being interpreted differently than you intended. Parenthesise aggressively.

**Fix.** Print the size in your load message and sanity-check against `diskinfo -v`.

### `newfs_ufs` Fails with "Device not configured"

**Symptom.** `newfs_ufs /dev/myblk0` prints `newfs: /dev/myblk0: Device not configured`.

**Likely causes.**

- Your strategy function is still the placeholder that returns `ENXIO` for everything. `ENXIO` is mapped to the `Device not configured` message by `errno`.

**Fix.** Implement the strategy function from Section 5.

### `newfs_ufs` Hangs

**Symptom.** `newfs_ufs /dev/myblk0` starts up but never completes.

**Likely causes.**

- Your strategy function does not call `biodone` on some path. `newfs_ufs` issues a BIO, waits for its completion, and will wait forever if completion never comes.
- Your strategy function calls `biodone` twice on some path. The first call returns success; the second call usually panics, but in some cases the BIO state is corrupted enough to hang.

**Fix.** Audit your strategy function. Every control-flow path must end with exactly one call to `biodone(bp)`. A useful pattern is to use a single exit point at the end of the function.

### `mount` Fails with "bad superblock"

**Symptom.** `mount /dev/myblk0 /mnt/myblk` reports `mount: /dev/myblk0: bad magic`.

**Likely causes.**

- Your strategy function is returning wrong data for some offsets. The superblock is at offset 65536, and UFS validates it carefully.
- Your bounds check is rejecting a legitimate read.
- Your `memcpy` is copying from the wrong address (usually an off-by-one in the offset arithmetic).

**Fix.** Write a known pattern to the device with `dd`, then read it back with `dd` at various offsets and compare with `cmp`. If the pattern round-trips, the basic I/O is correct. If not, find the first offset where it diverges and inspect the code at the corresponding bounds check or address arithmetic.

### `kldunload` Hangs

**Symptom.** `kldunload myblk` does not return.

**Likely causes.**

- A BIO is in flight and your strategy function never calls `biodone`. `disk_destroy` is waiting for the BIO to complete.
- You added a worker thread and it is sleeping inside a function that will never be awakened.

**Fix.** Run `procstat -kk` in another terminal. Look at the `g_event` thread's stack and at any of your driver's threads. If they are stuck in a `sleep` or `waitfor` state, you have a leaked BIO or a misbehaving worker.

### `kldunload` Returns "Device busy"

**Symptom.** `kldunload myblk` reports `Device busy` and exits.

**Likely causes.**

- A filesystem is still mounted on `/dev/myblk0`.
- A program still has `/dev/myblk0` open for raw access.
- A `dd` from a previous terminal session is still running in the background.

**Fix.** Run `mount | grep myblk` to check for active mounts. Run `fuser /dev/myblk0` to find open handles. Unmount, close, then unload.

### Kernel Panic with "freeing free memory"

**Symptom.** The kernel panics with a message about freeing already-freed memory, showing a stack trace through your driver.

**Likely causes.**

- The detach path is freeing the softc or the backing twice.
- A worker thread survived `disk_destroy` and tried to access freed state.

**Fix.** Review the detach order. Destroy the disk first (which waits for in-flight BIOs), then free the backing, then destroy the mutex, then free the softc. If you added a worker thread, make sure it has exited before any `free` is called.

### Kernel Panic with "vm_fault: kernel mode"

**Symptom.** The kernel panics with a page fault inside your driver, typically in the strategy function or in the detach path.

**Likely causes.**

- You dereferenced a null or freed pointer. The most common case is using `sc->backing` after it has been freed.
- You confused `bp->bio_data` with `bp->bio_disk` and read from the wrong pointer.

**Fix.** Audit pointer lifetimes. If the backing store is freed during detach, ensure no BIOs can still reach the strategy function after that point. The order `disk_destroy` -> `free(backing)` is the correct order.

### `gstat` Shows No Activity

**Symptom.** You are running `dd` or `newfs_ufs` against the device, but `gstat -f myblk0` shows zero ops/s.

**Likely causes.**

- You are watching the wrong device. `gstat -f myblk0` uses a regex; make sure your device name matches.
- Your driver uses a custom GEOM class name that `gstat` is filtering out.

**Fix.** Run `gstat` without the filter and look for your device. Check the name field carefully.

### "Operation not supported" for DELETE

**Symptom.** Mount-with-trim fails or `fstrim` prints "Operation not supported".

**Likely causes.**

- Your strategy function does not handle `BIO_DELETE` and returns `EOPNOTSUPP`.
- The filesystem probed `BIO_DELETE` support during mount and cached the negative result.

**Fix.** Implement `BIO_DELETE` in the strategy function, then unmount and remount. Most filesystems only probe at mount time.

### /dev/myblk0 Does Not Appear Until Several Seconds After kldload

**Symptom.** Immediately after `kldload`, `ls /dev/myblk0` fails. A few moments later, it succeeds.

**Likely causes.**

- GEOM processes events asynchronously. `disk_create` queues an event, and the provider is not published until the event thread picks it up.
- On a system under load, the event queue may be slow.

**Fix.** This is normal behaviour. If your scripts depend on the node existing immediately after `kldload`, add a small sleep or a polling loop.

### Data Written Is Readable but Garbled

**Symptom.** A read after a write returns the right number of bytes but with different content.

**Likely causes.**

- Off-by-one in the backing-store offset arithmetic.
- A concurrent BIO is overlapping with the one you expect, and your lock is not held for long enough.
- The strategy function is reading from `bp->bio_data` before the kernel has finished setting it up (this is extremely unlikely for normal BIOs, but can happen with bugs in how you parse attributes).

**Fix.** Add a `printf` to the strategy function that logs the first few bytes before and after the `memcpy`. Repeat the test with a known pattern and look for the mismatch.

### Backing Store Not Freed, Memory Grows With Every Reload

**Symptom.** `vmstat -m | grep myblk` shows the allocation bytes growing with each load/unload cycle.

**Likely causes.**

- The `MOD_UNLOAD` handler returned without calling `myblk_detach_unit`, so the `free(sc->backing, M_MYBLK)` was skipped.
- An error path in `MOD_UNLOAD` returned early before reaching the free. Every error path needs to free or the allocation leaks.
- A worker thread is holding a reference to the softc and the handler refuses to free while that reference exists.

**Fix.** Audit the `MOD_UNLOAD` path. `vmstat -m` is a blunt but effective tool. Add a `printf` in the free path to confirm it is being reached.

### `gstat` Shows Very High Queue Length

**Symptom.** `gstat -I 1` shows `L(q)` rising into the tens or hundreds and never returning to zero.

**Likely causes.**

- Your strategy function is slow or blocking, causing BIOs to queue faster than they are serviced.
- You added a worker thread but it is scheduled less often than it should be.
- A synchronisation bottleneck (a heavily contested mutex) is serialising the work.

**Fix.** Profile with DTrace to find what the strategy function is doing. If the latency per BIO has grown, investigate why. For an in-memory driver, this should almost never happen; if it does, you have likely introduced a `vn_rdwr` or other blocking call into the hot path.

### Strategy Function Called With NULL bio_disk

**Symptom.** Kernel panic in the strategy function when dereferencing `bp->bio_disk->d_drv1`.

**Likely causes.**

- The BIO was synthesised incorrectly by code outside your driver.
- You are accessing `bp->bio_disk` from the wrong context. In some GEOM paths, `bp->bio_disk` is only valid inside the strategy function of a `g_disk` driver.

**Fix.** If you need to access the softc, do it at the start of the strategy function. Cache the pointer in a local variable. Do not access `bp->bio_disk` from a different thread or from a deferred callback.

### Mysterious I/O Errors After Reload

**Symptom.** After `kldunload` and `kldload`, reads return EIO on offsets that worked before the unload.

**Likely causes.**

- You are working with a file-backed or vnode-backed experiment (from Lab 5 or from your own modifications) and the file's size or contents have been changed between loads.
- A type mismatch between the saved and new offsets (for instance, changing `d_sectorsize` between loads).
- `d_mediasize` has changed but the underlying file still reflects the old layout.

**Fix.** Ensure the backing file and the driver's geometry agree on both size and sector layout. If you change `d_mediasize` or `d_sectorsize`, regenerate the backing file to match. For a straightforward reload with no changes, the buffer on a RAM-backed driver is always fresh, so mysterious post-reload EIOs usually point to a geometry mismatch rather than data loss.

### Access Count Stuck at Non-Zero After Unmount

**Symptom.** After `umount`, `geom disk list` still shows non-zero access counts.

**Likely causes.**

- A program still has the raw device open. `fuser /dev/myblk0` will reveal it.
- The filesystem did not unmount cleanly. Check `mount | grep myblk` to see if it is still mounted.
- A lingering NFS client or similar is holding the filesystem open. Unlikely for a local memory disk, but possible on shared systems.

**Fix.** Find and close the open handle. If `umount` reports success but the access count remains, rebooting is the safest recovery.

### Driver Loaded But Not Visible in geom -t

**Symptom.** `kldstat` shows the module loaded, but `geom -t` does not show any geom of our name.

**Likely causes.**

- The loader ran but never called `disk_create`.
- `disk_create` was called but the event thread has not run yet.

**Fix.** Add a `printf` to confirm `disk_create` ran. Wait one or two seconds after `kldload` before checking, to give the event thread a chance.

### Panic on Second Load

**Symptom.** Loading the module once works. Unloading works. Loading a second time panics.

**Likely causes.**

- A `MOD_UNLOAD` handler did not reset all the state that `MOD_LOAD` assumes is fresh.
- A static pointer holds a reference to a freed structure across the unload boundary; the next load sees a dangling pointer.
- A GEOM class that was registered on first load did not unregister.

**Fix.** Audit your load and unload paths as a matched pair. Every allocation on load needs a corresponding free on unload, and every pointer written on load needs to be cleared on unload. For GEOM classes, `DECLARE_GEOM_CLASS` handles the unregistration for you, but if you bypass it you must do the work.

### newfs_ufs Aborts with "File system too small"

**Symptom.** `newfs_ufs /dev/myblk0` aborts with `newfs: /dev/myblk0: partition smaller than minimum UFS size`.

**Likely causes.**

- `MYBLK_MEDIASIZE` is too small for UFS's minimum practical size.
- You forgot to rebuild the module after changing the size.

**Fix.** Ensure the media size is at least a few megabytes. UFS's absolute minimum is around 1 MiB, but practical minimums are 4-8 MiB and comfortable minimums are 32 MiB or more.

### mount -o trim Does Not Trigger BIO_DELETE

**Symptom.** Mounting with `-o trim` succeeds, but `gstat` shows no delete operations even during heavy deletion.

**Likely causes.**

- UFS issues `BIO_DELETE` only on certain patterns; it does not unconditionally trim every freed block.
- Your driver does not advertise `BIO_DELETE` support in its `d_flags`.

**Fix.** Set `sc->disk->d_flags |= DISKFLAG_CANDELETE;` before `disk_create`. This tells GEOM and filesystems that your driver supports `BIO_DELETE` and is willing to handle them.

### UFS Complains About "Fragment out of bounds"

**Symptom.** After a mount, UFS logs an error about a fragment being out of bounds, and file operations start returning EIO.

**Likely causes.**

- Your driver is returning wrong data on some offset, and UFS has read a corrupted metadata block.
- The backing store was partially overwritten during some other test.
- Bounds-check arithmetic is returning incorrect ranges.

**Fix.** Unmount, run `fsck_ffs -y /dev/myblk0` to repair, then re-test. If the error recurs with fresh filesystems, look for offset-computation bugs in the strategy function.

### Kernel Printing "interrupt storm" Messages

**Symptom.** `dmesg` shows messages about interrupt storms, and system responsiveness degrades.

**Likely causes.**

- A real hardware driver (not yours) is misbehaving.
- Your driver is fine; this is a different subsystem's problem.

**Fix.** Verify that the storm is not related to your module. If it is, the issue is almost certainly in an interrupt handler, which our pseudo driver does not have.

### Reboot Hangs on Unmount During Shutdown

**Symptom.** On shutdown, the system hangs while unmounting, with a message like "Syncing disks, vnodes remaining...".

**Likely causes.**

- A filesystem is still mounted on your device, and your driver is holding a BIO.
- A syncer thread is stuck waiting for completion.

**Fix.** Ensure your driver unmounts cleanly before system shutdown. A robust way is to add a `shutdown_post_sync` event handler that unmounts the filesystem and unloads the module. For development, unmount and unload manually before issuing `shutdown -r now`.

### General Advice

Whenever something goes wrong, the first step is to read `dmesg` and look for messages from your own printfs and from kernel subsystems. The second step is to run `procstat -kk` and look at what threads are doing. The third step is to consult `gstat`, `geom disk list`, and `geom -t` for the storage topology. These three tools will tell you most of what you need in nearly every case.

If a panic happens, FreeBSD drops you into the debugger. Capture a backtrace with `bt` and a register dump with `show registers`, then reboot with `reboot`. If a crash dump was taken, `kgdb` on `/var/crash/vmcore.last` will let you inspect the state offline. Keeping crash dumps around, at least in a development environment, pays off immediately when you are chasing intermittent bugs.

And above all, when something fails, try to reproduce it. Intermittent bugs in storage drivers are almost always caused by timing differences in how many BIOs are in flight, how long they take, and when the scheduler decides to run your thread. If you can find a reliable reproduction, you are most of the way to a fix.

## Wrapping Up

This has been a long chapter. Let us take a moment to step back and see what we have covered.

We started by situating storage drivers in FreeBSD's layered architecture. The Virtual File System layer sits between system calls and filesystems, giving every filesystem a common shape. `devfs` is itself a filesystem, providing the `/dev` directory that user-space tools and administrators use to refer to devices. Storage drivers do not live inside VFS. They live at the bottom of the stack, below GEOM and below the buffer cache, and they communicate with the rest of the kernel through `struct bio`.

We built a working pseudo block device driver from scratch. In Section 3 we wrote the skeleton that registered a disk with `g_disk` and published a `/dev` node. In Section 4 we explored GEOM's concepts of classes, geoms, providers, and consumers, and we understood how the topology composes and how access counts keep the system safe during teardown. In Section 5 we implemented the strategy function that actually services `BIO_READ`, `BIO_WRITE`, `BIO_DELETE`, and `BIO_FLUSH` against an in-memory backing store. In Section 6 we formatted the device with `newfs_ufs`, mounted a real filesystem on it, and saw the two access paths (raw and filesystem) converge in our strategy function. In Section 7 we surveyed persistence options and added a simple technique for surviving module reloads. In Section 8 we walked through the teardown path in detail and learned how to test it. In Section 9 we looked at the directions a growing driver tends to go: multi-unit support, ioctl surfaces, source-file splits, and stable versioning.

We exercised the driver through labs and stretched it with challenges. We collected the common failure modes in a troubleshooting section. And throughout, we kept our eyes on the real FreeBSD source tree, because the goal of this book is not to teach toy kernel code but to teach the real thing.

You should now be able to read `md(4)` with real comprehension rather than just staring at it. You should be able to read `g_zero.c` and recognise every function it calls. You should be able to diagnose the common classes of storage-driver bug by symptom. And you should have a working, if simple, pseudo block device that you wrote yourself.

That is a substantial amount of ground covered. Take a moment to notice how far you have come. In Chapter 26 you knew how to write a character driver. Now you can also write a block driver. The two chapters together give you the foundation for nearly every other kind of driver in FreeBSD, because most drivers are either character-oriented or block-oriented at the boundary where they meet the rest of the kernel.

### A Summary of the Key Moves

For quick recall, here are the moves that define a minimal storage driver.

1. Include the right headers: `sys/bio.h`, `geom/geom.h`, `geom/geom_disk.h`.
2. Allocate a `struct disk` with `disk_alloc`.
3. Fill in `d_name`, `d_unit`, `d_strategy`, `d_sectorsize`, `d_mediasize`, `d_maxsize`, and `d_drv1`.
4. Call `disk_create(sc->disk, DISK_VERSION)`.
5. In `d_strategy`, switch on `bio_cmd` and service the request. Always call `biodone` exactly once.
6. In the unload path, call `disk_destroy` before freeing anything the strategy function touches.
7. Declare `MODULE_DEPEND` on `g_disk`.
8. Use `MAXPHYS` for `d_maxsize` unless you have a specific reason to be smaller.
9. Test the unload path under load. Test it with a mounted filesystem. Test it with a raw `cat` holding the device open.
10. Read `dmesg`, `gstat`, `geom disk list`, and `procstat -kk` when something goes wrong.

These ten moves are the skeleton of every FreeBSD storage driver you will ever write. They appear in different clothing in `ada(4)`, in `nvme(4)`, in `mmcsd(4)`, in `zvol(4)`, and in every other driver in the tree. Once you see the pattern, the variety across real drivers becomes much less mysterious.

### A Reminder About Raw Access

Even with a filesystem mounted, your driver is still reachable as a raw block device. `/dev/myblk0` remains a valid handle that tools like `dd`, `diskinfo`, `gstat`, and `dtrace` can use. The two access paths coexist through GEOM's discipline: both paths issue BIOs, both paths respect the access counts, and your strategy function services both without distinguishing between them. That uniformity is the great gift of GEOM to storage-driver authors.

### A Reminder About Safety

Working on a shared system while developing a storage driver is an invitation for pain. Use a virtual machine, or at the very least a system you can reinstall. Keep a rescue image handy. Keep backups of anything you cannot afford to lose, including code you are in the middle of writing. The chapter's driver is well-behaved and should not damage anything, but drivers you write in the future may not be, and the cost of being prepared is very small compared to the cost of being unprepared.

### Where to Look Next in the FreeBSD Tree

If you want to keep exploring storage before the next chapter, three areas of the tree repay careful reading.

- `/usr/src/sys/geom/` has the GEOM framework itself, including `g_class`, `g_disk`, and many transformation classes like `g_mirror`, `g_stripe`, and `g_eli`.
- `/usr/src/sys/dev/md/md.c` is the full-featured memory-disk driver, already mentioned many times in this chapter.
- `/usr/src/sys/ufs/` is the UFS filesystem. Not required reading for driver work, but it helps to see the layer immediately above yours.

Reading these is not a prerequisite for the next chapter. It is a recommendation for your own growth.

## Bridge to the Next Chapter

In this chapter we built a storage driver from scratch. The data flowing through it was internal to the system: bytes written to a file, bytes read from a file, superblocks and cylinder groups and inodes shuffling around in the buffer cache. No byte ever left the machine. The driver's entire world was the kernel's own memory and the processes that consume it.

Chapter 28 takes us into a different world. We will write a network interface driver. Network drivers are transport drivers like the USB serial driver of Chapter 26 and the storage driver of this chapter, but their conversation partner is not a process and not a filesystem. It is a network stack, and the unit of work is not a byte range and not a block but a packet. The packet is a structured object with headers and payload, and the driver participates in a stack that includes IP, ARP, ICMP, TCP, UDP, and many other protocols.

The patterns you have internalised in this chapter will reappear, with different names. Instead of `struct bio`, you will see `struct mbuf`. Instead of `g_disk`, you will see the `ifnet` interface. Instead of `disk_strategy`, you will see the `if_transmit` and `if_input` hooks. Instead of GEOM providers and consumers, you will see network interface objects linked into the kernel's network stack. The role is the same: a transport driver takes requests from above, delivers them below, accepts responses below, and delivers them above.

Many of the concerns will also be the same. Locking. Hot unplug. Resource cleanup on detach. Observability through kernel tools. Safety in the face of errors. The fundamentals carry over. What changes is the vocabulary, the structure of the unit of work, and some of the specific tools.

Before you move on, take a short break. Unload your storage driver. Run `kldstat` and confirm that nothing from this chapter is still loaded. Close your lab logbook. Stand up. Refill your coffee. The next chapter is going to be just as substantive as this one, and you will want a clear head.

When you come back, Chapter 28 will start the same way this one did: with a gentle introduction and a clear picture of where we are going. See you there.

## Quick Reference

The tables below are intended as a quick lookup when you are writing or debugging a storage driver and need to remember a name, a command, or a path. They are not a substitute for the full explanations earlier in the chapter.

### Key Headers

| Header | Defines |
|--------|---------|
| `sys/bio.h` | `struct bio`, `BIO_READ`, `BIO_WRITE`, `BIO_DELETE`, `BIO_FLUSH`, `BIO_GETATTR` |
| `geom/geom.h` | `struct g_class`, `struct g_geom`, `struct g_provider`, `struct g_consumer`, topology primitives |
| `geom/geom_disk.h` | `struct disk`, `DISK_VERSION`, `disk_alloc`, `disk_create`, `disk_destroy`, `disk_gone` |
| `sys/module.h` | `DECLARE_MODULE`, `MODULE_VERSION`, `MODULE_DEPEND` |
| `sys/malloc.h` | `MALLOC_DEFINE`, `malloc`, `free`, `M_WAITOK`, `M_NOWAIT`, `M_ZERO` |
| `sys/lock.h`, `sys/mutex.h` | `struct mtx`, `mtx_init`, `mtx_lock`, `mtx_unlock`, `mtx_destroy` |

### Key Structures

| Structure | Role |
|-----------|------|
| `struct disk` | The `g_disk` representation of a disk. Filled in by the driver, owned by the framework. |
| `struct bio` | One I/O request, passed between GEOM layers and into the driver's strategy function. |
| `struct g_provider` | The producer-facing interface of a geom. Filesystems and other geoms consume from providers. |
| `struct g_consumer` | The connection from one geom into another geom's provider. |
| `struct g_geom` | An instance of a `g_class`. |
| `struct g_class` | The template from which geoms are created. Defines methods like `init`, `fini`, `start`, `access`. |

### Common BIO Commands

| Command | Meaning |
|---------|---------|
| `BIO_READ` | Read bytes from the device into a buffer. |
| `BIO_WRITE` | Write bytes from a buffer into the device. |
| `BIO_DELETE` | Discard a range of blocks. Used for TRIM. |
| `BIO_FLUSH` | Commit outstanding writes to durable storage. |
| `BIO_GETATTR` | Query a named attribute from the provider. |
| `BIO_ZONE` | Zoned-block-device operations. Not commonly used. |

### Common GEOM Tools

| Tool | Purpose |
|------|---------|
| `geom disk list` | List registered disks and their providers. |
| `geom -t` | Show the entire GEOM topology as a tree. |
| `geom part show` | Show partition geoms and their providers. |
| `gstat` | Live per-provider I/O statistics. |
| `diskinfo -v /dev/xxx` | Show disk geometry and attributes. |
| `iostat -x 1` | Live per-device throughput and latency. |
| `dd if=... of=...` | Raw block I/O for testing. |
| `newfs_ufs /dev/xxx` | Create a UFS filesystem on a device. |
| `mount /dev/xxx /mnt` | Mount a filesystem. |
| `umount /mnt` | Unmount a filesystem. |
| `mdconfig` | Create or destroy memory disks. |
| `fuser` | Find processes holding a file open. |
| `procstat -kk` | Show kernel stack traces for all threads. |

### Key Callback Typedefs

| Typedef | Purpose |
|---------|---------|
| `disk_strategy_t` | Handles BIOs. The core I/O function. Required. |
| `disk_open_t` | Called when a new access is being granted. Optional. |
| `disk_close_t` | Called when an access is being released. Optional. |
| `disk_ioctl_t` | Handles ioctls on the `/dev` node. Optional. |
| `disk_getattr_t` | Answers `BIO_GETATTR` queries. Optional. |
| `disk_gone_t` | Notifies the driver when the disk is being forced away. Optional. |

### File and Path Reference

| Path | What lives there |
|------|------------------|
| `/usr/src/sys/geom/geom_disk.c` | The `g_disk` implementation. |
| `/usr/src/sys/geom/geom_disk.h` | The public `g_disk` interface. |
| `/usr/src/sys/geom/geom.h` | Core GEOM structures and functions. |
| `/usr/src/sys/sys/bio.h` | The `struct bio` definition. |
| `/usr/src/sys/dev/md/md.c` | The reference memory-disk driver. |
| `/usr/src/sys/geom/zero/g_zero.c` | A minimal GEOM class, useful as a reading reference. |
| `/usr/src/sys/ufs/ffs/ffs_vfsops.c` | UFS's mount path. Read if you want to see what mount does at the filesystem side. |
| `/usr/src/share/man/man9/disk.9` | The `disk(9)` manual page. |
| `/usr/src/share/man/man9/g_bio.9` | The `g_bio(9)` manual page. |

### Common Disk Flags

| Flag | Meaning |
|------|---------|
| `DISKFLAG_CANDELETE` | The driver handles `BIO_DELETE`. |
| `DISKFLAG_CANFLUSHCACHE` | The driver handles `BIO_FLUSH`. |
| `DISKFLAG_UNMAPPED_BIO` | The driver accepts unmapped BIOs (advanced). |
| `DISKFLAG_WRITE_PROTECT` | The device is read-only. |
| `DISKFLAG_DIRECT_COMPLETION` | Completion is safe from any context (advanced). |

These flags are set on `sc->disk->d_flags` before `disk_create`. They let the kernel make smarter choices about how to issue BIOs to your driver.

### Patterns for d_strategy

Here are the three most common shapes of a strategy function.

**Pattern 1: Synchronous, in-memory.** Our driver uses this. The function serves the BIO inline and returns after calling `biodone`.

```c
void strategy(struct bio *bp) {
    /* validate */
    switch (bp->bio_cmd) {
    case BIO_READ:  memcpy(bp->bio_data, ...); break;
    case BIO_WRITE: memcpy(..., bp->bio_data); break;
    }
    bp->bio_resid = 0;
    biodone(bp);
}
```

**Pattern 2: Enqueue to a worker thread.** `md(4)` uses this. The function attaches the BIO to a queue and signals a worker.

```c
void strategy(struct bio *bp) {
    mtx_lock(&sc->lock);
    TAILQ_INSERT_TAIL(&sc->queue, bp, bio_queue);
    wakeup(&sc->queue);
    mtx_unlock(&sc->lock);
}
```

The worker dequeues BIOs, services them one at a time (perhaps calling `vn_rdwr` or issuing hardware commands), and completes each with `biodone`.

**Pattern 3: Hardware DMA with interrupt completion.** Real hardware drivers use this. The function programs the hardware, sets up DMA, and returns. A later interrupt handler completes the BIO.

```c
void strategy(struct bio *bp) {
    /* validate */
    program_hardware(bp);
    /* strategy returns, interrupt will call biodone eventually */
}
```

Each pattern has trade-offs. Pattern 1 is simplest but cannot block. Pattern 2 handles blocking work but adds latency. Pattern 3 is needed for real hardware but requires interrupt handling, which adds a whole other layer of complexity.

Our chapter driver uses Pattern 1. `md(4)` uses Pattern 2. `ada(4)`, `nvme(4)`, and friends use Pattern 3.

### Minimum Registration Sequence

For the driver writer in a hurry, the minimum sequence to register a block device is:

```c
sc->disk = disk_alloc();
sc->disk->d_name       = "myblk";
sc->disk->d_unit       = sc->unit;
sc->disk->d_strategy   = myblk_strategy;
sc->disk->d_sectorsize = 512;
sc->disk->d_mediasize  = size_in_bytes;
sc->disk->d_maxsize    = MAXPHYS;
sc->disk->d_drv1       = sc;
disk_create(sc->disk, DISK_VERSION);
```

And the minimum teardown sequence is:

```c
disk_destroy(sc->disk);
free(sc->backing, M_MYBLK);
mtx_destroy(&sc->lock);
free(sc, M_MYBLK);
```

## Glossary

**Access count.** A tuple of three counters on a GEOM provider that tracks how many readers, writers, and exclusive holders currently have access to it. Displayed as `rNwNeN` in `geom disk list`.

**Attach.** In the Newbus sense, the step where a driver takes responsibility for a device. In the storage sense, the step where the driver calls `disk_create` to register with `g_disk`. The word overloads; use context.

**Backing store.** The place where the bytes of a storage device actually live. For our driver, the backing store is a `malloc`'d buffer in kernel memory. For real disks, it is the platter or the flash. For `md(4)` in vnode mode, it is a file in the host filesystem.

**BIO.** A `struct bio`. The unit of I/O that flows through GEOM.

**BIO_DELETE.** A BIO command that asks the driver to discard a range of blocks. Used for TRIM on SSDs.

**BIO_FLUSH.** A BIO command that asks the driver to make all previous writes durable before returning.

**BIO_GETATTR.** A BIO command that asks the driver to return the value of a named attribute.

**BIO_READ.** A BIO command that asks the driver to read a range of bytes.

**BIO_WRITE.** A BIO command that asks the driver to write a range of bytes.

**Block device.** A device that is addressed in fixed-size blocks, with seekable random access. Historically distinct from character devices in BSD; in modern FreeBSD, block and character access converge through GEOM but the mental distinction still matters.

**Buffer cache.** The kernel subsystem that holds recently used filesystem blocks in RAM. Sits between filesystems and GEOM. Not to be confused with the page cache; they are related but distinct in FreeBSD.

**Cache coherency.** The property that reads and writes see each other in a consistent order. The strategy function must not return data that is stale relative to recent writes on the same offset.

**Cdev.** A character device node as represented by `struct cdev`. Character drivers create them with `make_dev`. Block drivers usually do not.

**Consumer.** The input-facing side of a geom. A consumer attaches to a provider and issues BIOs into it.

**d_drv1.** A generic pointer in `struct disk` where the driver stores its private context, typically the softc.

**d_mediasize.** The total size of the device in bytes.

**d_maxsize.** The largest single BIO the driver can accept. Usually `MAXPHYS` for pseudo devices.

**d_sectorsize.** The size of a sector in bytes. Typically 512 or 4096.

**d_strategy.** The driver's BIO-handling function.

**Devfs.** A pseudo-filesystem mounted at `/dev` that synthesises file nodes for kernel devices.

**Devstat.** The kernel's device statistics subsystem, used by `iostat`, `gstat`, and others. Storage drivers using `g_disk` get devstat integration automatically.

**Disk_alloc.** Allocates a `struct disk`. Never fails; uses `M_WAITOK` internally.

**Disk_create.** Registers a filled-in `struct disk` with `g_disk`. The real work is done asynchronously.

**Disk_destroy.** Unregisters and destroys a `struct disk`. Waits for in-flight BIOs to complete. Panics if the provider still has users.

**Disk_gone.** Notifies `g_disk` that the underlying media is gone. Used in hot-unplug scenarios. Distinct from `disk_destroy`.

**DISK_VERSION.** The ABI version of the `struct disk` interface. Defined in `geom_disk.h` and passed to `disk_create`.

**DTrace.** FreeBSD's dynamic tracing facility. Especially useful for observing BIO traffic.

**Event thread.** The single kernel thread that GEOM uses to process topology events such as creating and destroying geoms. Usually called `g_event` in `procstat` output.

**Exclusive access.** A kind of access on a provider that forbids other writers. Filesystems acquire exclusive access on the devices they mount.

**Filesystem.** A concrete implementation of file storage semantics, such as UFS, ZFS, tmpfs, or NFS. Plugs into VFS.

**GEOM.** The FreeBSD framework for composable block-layer transformations. Classes, geoms, providers, and consumers are its main objects.

**g_disk.** The GEOM subsystem that wraps disk-shaped drivers with a simpler API. Our driver uses it.

**g_event.** The GEOM event thread that processes topology changes.

**g_io_deliver.** The function used at the class level to complete a BIO. `g_disk` calls it for us; our driver calls `biodone`.

**g_io_request.** The function used at the class level to issue a BIO downward. Only used in drivers that implement their own GEOM class.

**Hotplug.** A device that can appear or disappear without reboot.

**Ioctl.** A control operation on a device, distinct from read or write. In the storage path, ioctls on `/dev/diskN` go through GEOM and may be handled by `g_disk` or by the driver's `d_ioctl`.

**md(4).** The FreeBSD memory-disk driver. The canonical pseudo block device in the tree, and a recommended reading reference.

**Mount.** The act of attaching a filesystem to a point in the namespace. Calls VFS, which calls the filesystem's own mount routine, which typically opens a GEOM provider.

**Newbus.** FreeBSD's bus framework. Used for character and hardware drivers. Our storage driver does not directly use Newbus because it is a pseudo device; real storage drivers almost always do.

**Provider.** The output-facing side of a geom. Other geoms or `/dev` nodes consume providers.

**Softc.** The per-instance state structure of a driver.

**Strategy function.** The driver's BIO handler. Called `d_strategy` in the `struct disk` API.

**Superblock.** A small on-disk structure that describes the layout of a filesystem. UFS's is at offset 65536.

**Topology.** The tree of GEOM classes, geoms, providers, and consumers. Protected by the topology lock.

**Topology lock.** The global lock protecting the GEOM topology from concurrent modification.

**UFS.** The Unix File System, FreeBSD's default filesystem. Lives under `/usr/src/sys/ufs/`.

**Unit.** A numbered instance of a driver. `myblk0` is unit 0 of the `myblk` driver.

**VFS.** The Virtual File System layer. Sits between system calls and concrete filesystems.

**Vnode.** The kernel's runtime handle on an open file or directory. Lives inside VFS.

**Withering.** The GEOM process of removing a provider from the topology. Queued on the event thread, waits for in-flight BIOs, and finally destroys the provider.

**Zone.** In VM-subsystem vocabulary, a pool of fixed-size objects allocated through the UMA (Universal Memory Allocator). Many kernel structures, including BIOs and GEOM providers, are allocated from zones.

**BIO_ORDERED.** A BIO flag that asks the driver to execute this BIO only after all previously issued BIOs have completed. Used for write barriers.

**BIO_UNMAPPED.** A BIO flag that indicates `bio_data` is not a mapped kernel virtual address but rather a list of unmapped pages. Drivers that can handle unmapped data should set `DISKFLAG_UNMAPPED_BIO`.

**Direct completion.** Completing a BIO in the same thread that submitted it, without going through a deferred callback. Usually faster but not always safe.

**Drivers in Tree.** Drivers that live inside the FreeBSD source tree and are built as part of the standard kernel build. Contrast with out-of-tree drivers, which are maintained separately.

**Out-of-tree driver.** A driver that is not part of the FreeBSD source tree. These need to be compiled against a matching kernel and may need updates when the kernel's ABI changes.

**ABI.** Application Binary Interface. The set of conventions for function calling, structure layout, and type sizes that allow two pieces of compiled code to interoperate. `DISK_VERSION` is one kind of ABI marker.

**API.** Application Programming Interface. The set of function signatures and types that code uses at the source level. Distinct from ABI: two kernels with the same API might have different ABIs if they were compiled differently.

**KPI.** Kernel Programming Interface. FreeBSD's preferred term for the kernel's API. Guarantees about KPI stability are limited; always recompile against the kernel you are running.

**KLD.** Kernel loadable module. The `.ko` file we produce. The "KLD" stands for Kernel Loadable Driver, though modules are not necessarily drivers.

**Module.** See KLD.

**Taste.** In GEOM vocabulary, the process of offering a provider to all classes so that each class can decide whether to attach to it. Tasting happens automatically when new providers appear.

**Retaste.** Forcing GEOM to taste a provider again, usually after its contents have changed. `geom provider retaste` triggers this for one provider; `geom retaste` triggers it globally.

**Orphan.** In GEOM vocabulary, a provider whose underlying storage has gone away. Orphans are cleaned up by the event thread.

**Spoil.** A GEOM concept related to cache invalidation. If a provider's contents change in a way that could invalidate caches, it is said to have been spoiled.

**Bufobj.** A kernel object that associates a vnode (or a GEOM consumer) with a buffer cache. Each block device and each file has one.

**bdev_strategy.** A legacy synonym for `d_strategy`. Modern code uses `d_strategy` directly.

**Schedule.** The act of placing a BIO onto an internal queue for later execution. Distinct from "executing".

**Plug/unplug.** In some kernels, the plug is a batching mechanism for BIO submission. FreeBSD does not have plug/unplug; it delivers BIOs immediately.

**Elevator.** A BIO scheduler that sorts BIOs by offset to reduce disk seek time. FreeBSD's GEOM does not implement an elevator at the GEOM layer; it is the block device's responsibility, if relevant.

**Superblock.** The first metadata block of a filesystem. Describes geometry. At offset 65536 for UFS.

**Cylinder group.** A UFS concept. The filesystem is divided into regions, each with its own inode table and block allocation bitmap. Keeps related data physically close on a spinning disk and limits the damage a single bad region can cause.

**Inode.** A UFS (and POSIX) structure describing one file: its mode, owner, size, timestamps, and pointers to data blocks. Filenames live in directory entries, not in inodes.

**Vop_vector.** The dispatch table that a filesystem provides to VFS, listing all the operations VFS knows how to ask about (open, close, read, write, lookup, rename, and so on). VFS calls these as indirect function pointers.

**Devstat.** A kernel structure attached to devices that records aggregate I/O statistics. `iostat(8)` reads devstat data; `g_disk` allocates and feeds a devstat structure for every disk it creates.

**bp.** Shorthand in kernel source for a BIO pointer. Used almost universally in strategy functions and completion callbacks. When you see `struct bio *bp`, read it as "the current request".

**Bread.** Buffer-cache function that reads a block, consulting the cache first and issuing I/O only on a miss. Used by filesystems, not by drivers.

**Bwrite.** Buffer-cache function that writes a block synchronously. The filesystem uses it; your strategy function eventually sees the resulting BIO.

**Getblk.** Buffer-cache function that returns a buffer for a given block, allocating it if necessary. Used by filesystems as the entry point to both reading and writing.

**Bdwrite.** Delayed buffer-cache write. Marks the buffer dirty but does not issue I/O immediately. Will be written later by the syncer or by buffer cache pressure.

**Bawrite.** Asynchronous buffer-cache write. Like `bwrite` but does not wait for completion.

**Syncer.** A kernel thread that periodically flushes dirty buffers to their backing devices. Closing a filesystem cleanly requires the syncer to finish.

**Taskqueue.** A kernel facility for running callbacks in a separate thread. Useful when your strategy function wants to defer work. Covered in more depth when we discuss interrupt handlers in later chapters.

**Callout.** A kernel facility for scheduling a one-shot or periodic callback at a given time. Not commonly used in simple storage drivers but very common in hardware drivers that implement timeouts.

**Witness.** A kernel subsystem that detects lock-order violations and prints warnings. Always enabled in debug kernels; saves hours of debugging.

**INVARIANTS.** A kernel compile option that adds runtime assertions. Always enabled in debug kernels; catches many storage bugs before they become silent corruption.

**Debug kernel.** A kernel built with `INVARIANTS`, `WITNESS`, and related options. Slower but much safer for driver development. Use one during lab work.

## Frequently Asked Questions

### Do I need to support BIO_ORDERED?

For a pseudo device that services BIOs synchronously, no. Each BIO completes before the next one is processed, which trivially preserves ordering. For an asynchronous driver, you must respect `BIO_ORDERED` by deferring subsequent BIOs until the ordered one completes.

### What is the relationship between d_maxsize and MAXPHYS?

`d_maxsize` is the maximum BIO size your driver can accept. `MAXPHYS` is the compile-time upper bound on BIO size, defined in `/usr/src/sys/sys/param.h`. On 64-bit systems such as amd64 and arm64, `MAXPHYS` is 1 MiB; on 32-bit systems it is 128 KiB. FreeBSD 14.3 also exposes a runtime tunable, `maxphys`, which some subsystems consult through the `MAXPHYS` macro or the `maxphys` variable. Setting `d_maxsize = MAXPHYS` accepts whatever the kernel is willing to issue. For most pseudo drivers this is fine.

### Can my driver issue BIOs to itself?

Technically yes, but it rarely makes sense. The pattern is used by GEOM transformation classes (they take in BIOs from above and issue new BIOs downward). A `g_disk` driver is at the bottom of the stack and has no downward; if you need to split work across multiple backing units, you probably want worker threads rather than nested BIOs.

### Why do some fields in struct disk use u_int and others off_t?

`u_int` is used for unsigned integer sizes that fit in 32 bits (sector size, number of heads, etc.). `off_t` is a signed 64-bit type used for byte offsets and sizes that can exceed 32 bits (media size, request offsets). The distinction matters for large disks; a media size of 10 TB requires more than 32 bits.

### Is disk_alloc safe to call at any time?

`disk_alloc` uses `M_WAITOK` and will sleep if memory is tight. Do not call it while holding a spin lock or a mutex you cannot release. Call it at attach time, outside any lock.

### What happens if I call disk_create twice with the same name?

`disk_create` will happily create multiple disks with the same name if the unit numbers differ. If both the name and unit number match, GEOM will reject the second registration and the resulting behaviour is implementation-defined. Avoid this case.

### Can the strategy function sleep?

Technically yes, but it should not. The strategy function runs in the caller's thread context, and sleeping there stalls the caller. For work that must block, use a worker thread.

### How do I know when all BIOs have finished for a given filesystem?

You usually do not need to. `umount(2)` does the work: it flushes dirty buffers, drains in-flight BIOs, and returns only after the filesystem is fully quiesced. After `umount` returns, no BIOs will arrive for that mount point unless something else opens the device.

### Can I pass pointers between threads through bio_caller1 or similar fields?

Yes. `bio_caller1` and `bio_caller2` are opaque fields meant for the issuer of the BIO to stash context that the completion handler can use. As long as you own the BIO (which you do, because you issued it), the fields are yours. `g_disk` drivers do not usually need them because the BIO arrives from above and is completed by calling `biodone`, with `g_disk` handling the callback routing.

### My driver works on my laptop but not on the server. Why?

Possibilities: different kernel ABI (recompile against the server's kernel), different `MAXPHYS` (should be identical on 14.3 systems but check), different GEOM classes loaded (unlikely but possible), different memory size (your allocation might fail on a smaller system), different clock speed (affecting timing). Compare `uname -a` and `sysctl -a | grep kern.maxphys` to start.

### Where does the /dev node's name actually come from?

From `d_name` and `d_unit` in the `struct disk` you pass to `disk_create`. GEOM concatenates them without a separator: `d_name = "myblk"`, `d_unit = 0` produces `/dev/myblk0`. If you want a different convention, set `d_name` accordingly. There is no separator character between the name and the unit.

### What is the maximum number of units I can create?

Limited by `d_unit`, which is `u_int`, so 2^32 - 1 in theory. In practice, per-unit memory consumption and the practical limits of `/dev` name space will stop you long before that.

### Can I change d_mediasize after disk_create?

Yes, but carefully. Filesystems mounted on the disk will not pick up the change automatically; most will require unmount and remount. `md(4)` supports `MDIOCRESIZE` and there is infrastructure for signalling the change to GEOM, but the pattern is non-trivial.

### What happens if I forget MODULE_DEPEND?

The kernel may fail to load your module if `g_disk` is not already loaded, or may load it successfully if `g_disk` happens to be built into the kernel. Always declare `MODULE_DEPEND` explicitly to avoid surprise.

### Should I use biodone or g_io_deliver in my driver?

Use `biodone`. The `g_disk` wrapper provides a `d_strategy` style interface where the correct completion call is `biodone`. If you write your own `g_class`, you will call `g_io_deliver` instead, but that is a different path and a different chapter's worth of complexity.

### How does BIO_DELETE relate to TRIM and UNMAP?

`BIO_DELETE` is the in-kernel abstraction. For SATA SSDs it maps to the ATA TRIM command, for SCSI/SAS to UNMAP, and for NVMe to Dataset Management with the deallocate bit. Userland triggers it through `fstrim(8)` or the `-o trim` mount option on UFS. Our driver is free to treat it as a hint or to honour it by zeroing memory, since backing is in RAM.

### Why does my strategy function sometimes receive a BIO with bio_length of zero?

In normal operation you should never see this. If it happens, treat it as a defensive case: call `biodone(bp)` with no error and return. A length-zero BIO is not illegal, but it indicates something odd upstream. Filing a PR against the issuing code is reasonable.

### What is the difference between d_flags and bio_flags?

`d_flags` is static configuration for the whole disk, set once at registration and describing what the driver can do (handles DELETE, can FLUSH, accepts unmapped BIOs, and so on). `bio_flags` is dynamic metadata on a single BIO, changing per request (ordered, unmapped, direct completion). Do not confuse them.

### Can my driver present itself as removable media?

Yes, set `DISKFLAG_CANDELETE` and consider honouring `disk_gone` to simulate ejection. Tools like `camcontrol` and filesystem handlers generally treat any GEOM provider uniformly, so "removable" in the user-visible sense is less distinct than in other operating systems.

### What thread actually calls my strategy function?

It depends. For synchronous submission from the buffer cache, it is the thread that called `bwrite` or `bread`. For asynchronous completion paths it is often a GEOM worker or the buffer cache's flusher thread. Your strategy must be written to tolerate any caller. Do not assume a specific thread identity or a specific priority.

### How do I know which process caused a given BIO?

You usually cannot, because BIOs can be reordered, coalesced, merged, and issued from background threads that are not the original requester. `dtrace` with the `io:::start` probe plus stack captures can get you close, but it is investigative work, not a routine driver responsibility.

### Can two different filesystems be mounted simultaneously on two unit numbers of my driver?

Yes, if you implemented multi-unit support. Each unit presents its own GEOM provider. Their backing stores are independent. The only shared state is your module's global variables and the kernel itself, so the two mounts do not interact unless you make them.

### Should my driver handle power management events?

For a pseudo device, no. For a real hardware driver, yes: suspend and resume events flow through Newbus as method calls, and the driver must quiesce I/O on suspend and revalidate device state on resume. Storage drivers on laptops are a common source of suspend-related bugs, so real drivers take this seriously.

### What is the practical impact of choosing 512 versus 4096 as d_sectorsize?

On modern filesystems, very little: UFS, ZFS, and most other FreeBSD filesystems work happily with either. On the driver side, a larger sector size reduces the number of BIOs for large transfers. On the workload side, applications doing O_DIRECT or aligned I/O may care. When in doubt, pick 4096 for new drivers; it matches modern flash and avoids alignment penalties.

### If I reload my driver many times, does memory leak?

Only if you have a bug. In our design, `MOD_UNLOAD` calls `myblk_detach_unit`, which frees the backing store and the softc. The persistence variant deliberately retains the backing store across reloads but uses a single global pointer, so there is no leak; the same memory is reused. If `vmstat -m | grep myblk` climbs across reloads, investigate.

### Why does `mount` sometimes succeed on my raw device but `newfs_ufs` fail?

`newfs_ufs` writes structured metadata (superblock, cylinder groups, inode tables) and then reads some of it back to verify. If the device is too small, corrupts writes silently, or returns errors only under certain conditions, `newfs_ufs` catches it first. `mount` is much less strict at the write path; it can read in a broken superblock and produce odd errors later. A successful `newfs` is a stronger correctness signal than a successful `mount`.

### How do I verify that my BIO_FLUSH implementation actually makes data durable?

For our in-memory driver, durability is bounded by the host's power: flushing does nothing useful because a power cut takes everything with it. For a real driver backed by persistent storage, issuing a flush command to the underlying media and confirming completion before calling `biodone` is the contract. Testing requires a power-cycle harness or a simulator; there is no shortcut.

### What are the correct locking rules inside d_strategy?

Hold the driver's lock long enough to protect the backing store against concurrent access, and release it before calling `biodone`. Never hold a lock across a call into another subsystem. Never call `malloc(M_WAITOK)` with a lock held. Never sleep. If you need to sleep, schedule the work on a taskqueue and call `biodone` from the worker.

### Why is BIO_FLUSH not a percent-of-capacity barrier like write barriers on Linux?

FreeBSD's BIO_FLUSH is a point-in-time barrier: when it completes, all previously issued writes are durable. It is not associated with a particular range or percentage of the device. Drivers can implement it as a strict barrier or as an opportunistic flush, but the minimum contract is the point-in-time guarantee.

### Are there any tools that generate BIO traffic to help me test?

Yes. `dd(1)` with various `bs=` values, `fio(1)` from ports, `ioping(8)` from ports, plus the usual suspects: `newfs`, `tar`, `rsync`, `cp`. `diskinfo -t` runs a suite of benchmark reads and is useful for rough throughput numbers. The test harnesses under `/usr/src/tools/regression/` can also be adapted.

## What This Chapter Did Not Cover

This chapter is long, but there are several related topics we deliberately left for later. Naming them here helps you plan future study and prevents the false impression that storage drivers end at the BIO handler.

**Real hardware storage drivers** such as those for SATA, SAS, and NVMe controllers live under CAM and require significant additional machinery: command-block allocation, tagged queueing, hot-plug event handling, firmware upload, SMART data, and error-recovery protocols. We introduced the CAM world briefly through the `ada_da.c` excerpt but did not explore it in depth. Chapters 33 through 36 will tackle these interfaces, and the md(4) driver you read in this chapter is a deliberately small staircase by comparison.

**ZFS integration** is its own world. ZFS consumes GEOM providers through its vdev layer but adds copy-on-write semantics, end-to-end checksums, pooled storage, and snapshots that no simple block driver would ever need to know about. If your driver works under UFS it almost certainly works under ZFS, but the reverse is not guaranteed: ZFS exercises BIO paths, especially flushing and write ordering, that less demanding filesystems skip.

**GEOM class authoring** is a larger topic than `g_disk` wrapping. A full class implements taste, start, access, attach, detach, dumpconf, destroy_geom, and orphan methods. It can also create and destroy consumers, build multi-level topologies, and respond to configuration via `gctl`. The mirror, stripe, and crypt classes are good starting points once you decide to dig in.

**Quotas, ACLs, and extended attributes** are filesystem features that live above the GEOM layer entirely. They matter for userland but do not touch the storage driver. This is a useful piece of clarity: the driver's job ends at the BIO boundary.

**Tracing and debugging kernel crashes** deserves its own chapter. Kernel core dumps land on a dump device configured via `dumpon(8)`, analysed with `kgdb(1)` or `crashinfo(8)`. If your driver panics the system, being able to load the core file and inspect backtraces is a professional-level skill that this chapter only gestured at.

**High-performance storage paths** use features such as unmapped I/O, direct-dispatch completion, CPU pinning, NUMA-aware allocation, and dedicated queues. These optimisations matter for gigabytes-per-second workloads but are irrelevant to a teaching driver. When you start chasing microseconds, come back to `/usr/src/sys/dev/nvme/` and study how the real professionals do it.

**Filesystem-specific behaviour** varies widely. UFS asks for one set of BIOs; ZFS asks for a different set; msdosfs and ext2fs ask for something different again. A good storage driver is filesystem-agnostic, but observing different filesystems on your driver is a fantastic way to build intuition. Try `msdosfs`, `ext2fs`, and `tmpfs` for contrast after you are comfortable with UFS.

**iSCSI and network block devices** present themselves as GEOM providers too, but they are created by userland control daemons and talk to the network stack. Chapter 28 begins the networking work that makes those providers possible.

Our treatment of the storage path was deliberately focused. We wrote a driver that filesystems accept as real, we understood why and how it is seen that way, and we traced the data path from `write(2)` to RAM. That foundation is enough to make the unexplored topics above readable rather than bewildering.

## Final Reflection

Storage drivers have a reputation as forbidding territory. This chapter should have replaced some of that reputation with familiarity: the BIO is just a structure, the strategy function is just a dispatcher, GEOM is just a graph, and `disk_create` is just a registration call. What elevates storage work from routine is not the underlying APIs, which are compact, but the operational demands that accumulate around them: performance, durability, error recovery, and correctness under contention.

Those demands do not go away when you leave pseudo devices for real hardware. They multiply. But you already have the vocabulary to understand them. You know what a BIO is and where it comes from. You know which thread calls your code and what it expects. You know how to register with GEOM, how to unregister cleanly, and how to recognise an in-flight request from its shadow in `gstat`. When you sit in front of a real SATA controller driver and start reading, you will recognise the shape of the code even though the specifics differ.

The craft of storage-driver writing is, ultimately, patient. You learn by writing small drivers, reading the source tree, reproducing simple experiments, and building instincts about when something that looks right is actually right. The chapter you just finished is a single long step along that journey. The next chapters will step again, each time in different directions.

## Further Reading

If this chapter has whetted your appetite for storage internals, here are some places to go next.

**Manual pages**. `disk(9)`, `g_bio(9)`, `geom(4)`, `devfs(5)`, `ufs(5)`, `newfs(8)`, `mdconfig(8)`, `gstat(8)`, `diskinfo(8)`, `mount(2)`, `mount(8)`. Read them in that order.

**The FreeBSD Architecture Handbook**. The storage chapter is a good complement to this one.

**Kirk McKusick et al., "The Design and Implementation of the FreeBSD Operating System".** The book's chapters on the filesystem are especially relevant.

**DTrace books.** Brendan Gregg's "DTrace Book" is a practical reference; Sun's "Dynamic Tracing Guide" is the original tutorial.

**The FreeBSD source tree.** `/usr/src/sys/geom/`, `/usr/src/sys/dev/md/`, `/usr/src/sys/ufs/`, and `/usr/src/sys/cam/ata/` (where `ata_da.c` implements the `ada` disk driver). Every pattern discussed in this chapter is grounded in that code.

**The mailing list archives.** `freebsd-geom@` and `freebsd-fs@` are the two most relevant lists. Reading historic threads is one of the best ways to pick up the institutional knowledge that books do not capture.

**Commit history on GitHub mirrors.** The FreeBSD source tree has a long, well-annotated commit history. For any file you open, running `git log --follow` against its mirror will often reveal the rationale behind design choices, the bugs that shaped the current code, and the people who maintain it. Historical context makes the present code much easier to read.

**The Transactions of the FreeBSD Developer Summit.** Several summits have included storage-focused sessions. Recordings and slides, when available, are excellent for picking up the state of the art and the open design debates.

**Reading other operating systems' storage stacks.** Once you know FreeBSD's storage path, Linux's block layer, Illumos's SD framework, and macOS's IOKit storage classes all become comprehensible in a way they probably were not before. The specific APIs differ, but the fundamental shapes, BIOs or their equivalents, filesystems above, hardware below, are universal.

**Testing frameworks for kernel code.** The `kyua(1)` harness runs regression tests against real kernels. The `/usr/tests/sys/geom/` tree has examples of what well-written tests for storage code look like; reading them builds both testing instincts and confidence that your code is right.

**FreeBSD Foundation blog posts.** The Foundation funds several storage-related projects and publishes readable summaries that complement the source tree.

---

End of Chapter 27. Close your lab logbook, make sure your driver is unloaded and your mount points are released, and take a break before Chapter 28.

You have just written a storage driver, mounted a filesystem on it, traced data through the buffer cache, into GEOM, through your strategy function, and back again. That is a real accomplishment. Rest on it for a moment before you turn the page.
