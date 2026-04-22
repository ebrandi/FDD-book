---
title: "Working with Device Files"
description: "How devfs, cdevs, and device nodes give your driver a safe, well-shaped user surface."
partNumber: 2
partName: "Building Your First Driver"
chapter: 8
lastUpdated: "2026-04-17"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "TBD"
estimatedReadTime: 210
---

# Working with Device Files

## Reader Guidance & Outcomes

In Chapter 7 you built `myfirst`, a real FreeBSD driver that attaches cleanly, creates `/dev/myfirst0`, opens and closes that node, and unloads without leaks. That was the first win, and it was a real one. You now have a working driver skeleton on disk, a `.ko` file that the kernel will accept and release at your command, and a `/dev` entry that user programs can reach.

This chapter zooms in on the piece of that work that is easiest to take for granted: the **device file itself**. The line of code that created `/dev/myfirst0` in Chapter 7 was compact, but it sits on top of a subsystem called **devfs**, and that subsystem is the bridge between everything your driver does inside the kernel and every tool or program a user ever points at it. Understanding that bridge well now will make Chapters 9 and 10, where real data starts to flow, much less mysterious.

### Why This Chapter Earns Its Own Place

Chapter 6 introduced the device-file model at the level of mental pictures, and Chapter 7 used enough of it to get a driver running. Neither chapter stopped to examine the surface itself. That is not an oversight. In a book that teaches driver writing from first principles, the device file is worth a dedicated chapter because the mistakes made on that surface are also the mistakes hardest to undo later.

Consider what the surface has to carry. It carries identity (a path a user program can predict). It carries access policy (who is allowed to open, read, or write). It carries multiplexing (one driver, many instances, many simultaneous openers). It carries lifecycle (when the node appears, when it disappears, and what happens when a user program is mid-call as it goes). It carries compatibility (legacy names alongside modern ones). It carries observability (what operators can see and change from userland). A driver that gets the internals right but the surface wrong will be a driver that operators refuse to deploy, a driver that security reviewers flag, a driver that breaks subtly inside jails, a driver that deadlocks on unload under realistic load.

Chapter 7 gave you just enough of that surface to prove the path worked. This chapter gives you enough to design it on purpose.

### Where Chapter 7 Left the Driver

It is worth a short checkpoint on the state of `myfirst` before we extend it. Your Chapter 7 driver ends up with all of the following in place:

- A `device_identify`, `device_probe`, and `device_attach` path that creates exactly one Newbus child of `nexus0` named `myfirst0`.
- A softc allocated by Newbus, reachable through `device_get_softc(dev)`.
- A mutex, a sysctl tree under `dev.myfirst.0.stats`, and three read-only counters.
- A `struct cdevsw` populated with `d_open`, `d_close`, and stubs for `d_read` and `d_write`.
- A `/dev/myfirst0` node created with `make_dev_s(9)` in `attach` and removed with `destroy_dev(9)` in `detach`.
- A single-label error unwind that leaves the kernel in a consistent state if any attach step fails.
- An exclusive-open policy that refuses a second `open(2)` with `EBUSY`.

Chapter 8 treats that driver as the starting point and grows it along three axes: **shape** (what the node is called and how it is grouped), **policy** (who is allowed to use it and how that policy stays in place across reboots), and **per-descriptor state** (how two simultaneous openers can own separate book keeping).

### What You Will Learn

By the end of this chapter you will be able to:

- Explain what a device file is in FreeBSD, and why `/dev` is not an ordinary directory.
- Describe how `struct cdev`, the devfs vnode, and a user file descriptor relate to each other.
- Choose sensible ownership, group, and permission values for a new device node.
- Give a device node a structured name (including a subdirectory under `/dev`).
- Create an alias so a single cdev is reachable under more than one path.
- Attach per-open state with `devfs_set_cdevpriv()` and clean it up safely when the file descriptor closes.
- Adjust device-node permissions persistently from userland with `devfs.conf` and `devfs.rules`.
- Exercise your driver from a small userland C program, not just with `cat` and `echo`.

### What You Will Build

You will extend the Chapter 7 `myfirst` driver in three small steps:

1. **Stage 0: tidier permissions and a structured name.** The node moves from `/dev/myfirst0` to `/dev/myfirst/0` with a group-accessible variant for lab use.
2. **Stage 1: a user-visible alias.** You add `/dev/myfirst` as an alias for `/dev/myfirst/0` so legacy paths keep working.
3. **Stage 2: per-open state.** Each `open(2)` gets its own small counter with `devfs_set_cdevpriv()`, and you verify from userland that two simultaneous opens see independent values.

You will also write a short userland program, `probe_myfirst.c`, that opens the device, reads a bit, reports what it saw, and closes cleanly. This program will come back in Chapter 9 when real `read(2)` and `write(2)` paths are implemented.

### What This Chapter Does Not Cover

Several topics touch `/dev` but are deliberately postponed:

- **Full `read` and `write` semantics.** Chapter 7 stubbed these; Chapter 9 implements them properly with `uiomove(9)`. Here we only prepare the ground.
- **Cloning devices** (`clone_create`, the `dev_clone` event handler). These are worth a careful look later, once the basic model is solid.
- **`ioctl(2)` design.** Inspecting and changing device state through `ioctl` is a topic of its own and belongs later in the book.
- **GEOM and storage devices.** GEOM builds on top of cdevs but adds a whole stack of its own. That belongs in Part 6.
- **Network interface nodes and `ifnet`.** Network drivers do not live under `/dev`. They show up through a different surface, which we will meet in Part 6.

Keeping scope tight here is the point. The surface of a device is small; the discipline around it should be the first thing you master.

### Estimated Time Investment

- **Reading only:** about 30 minutes.
- **Reading plus the code changes to `myfirst`:** around 90 minutes.
- **Reading plus all four labs:** two to three hours, including rebuild cycles and userland testing.

A steady session with breaks works best. The chapter is shorter than Chapter 7, but the ideas here show up in almost every driver you will ever read.

### Prerequisites

- A working Chapter 7 `myfirst` driver that loads, attaches, and unloads cleanly.
- FreeBSD 14.3 in your lab with matching `/usr/src`.
- Basic comfort reading `/usr/src` paths such as `/usr/src/sys/dev/null/null.c`.

### How to Get the Most Out of This Chapter

Open your Chapter 7 source next to this chapter and edit the same file. You are not starting a new project; you are growing the one you already have. When the chapter asks you to inspect a FreeBSD file, really open it in `less` and scroll around. The device-file model clicks much faster when you have seen a couple of real drivers shape their nodes.

A practical habit that pays off immediately: as you read, keep a second terminal open against a freshly booted lab system and confirm every claim about existing nodes with `ls -l` or `stat(1)`. Typing `ls -l /dev/null` and watching the output match the prose is tiny, but it anchors the abstraction in something you can see. By the time the chapter reaches the labs, you will have a quiet reflex for verifying each claim against the running kernel rather than trusting the text alone.

A second habit: when the chapter names a source file under `/usr/src`, open it side by side with the section. Real FreeBSD drivers are the textbook; this book is only the reading guide. The material inside `/usr/src/sys/dev/null/null.c` and `/usr/src/sys/dev/led/led.c` is short enough to scan in a few minutes each, and each one is shaped by the very decisions this chapter is about to explain. A short tour there is worth more than any amount of prose here.

### Roadmap Through the Chapter

If you want a picture of the chapter as one continuous thread, here it is. The sections in order are:

1. What a device file actually is, in theory and in `ls -l` practice.
2. How devfs, the filesystem behind `/dev`, came to be and what it does for you.
3. The three kernel objects that line up behind a device file.
4. How ownership, group, and mode shape what `ls -l` shows and who can open the node.
5. How names get chosen, including unit numbers and subdirectories.
6. How one cdev can answer to several names through aliases.
7. How per-open state is registered, retrieved, and cleaned up.
8. How the destructor really works once `destroy_dev(9)` is called.
9. How `devfs.conf` and `devfs.rules` shape policy from userland.
10. How to drive the device from small userland programs you can write yourself.
11. How real FreeBSD drivers solve these same problems.
12. Which errno values your `d_open` should return, and when.
13. Which tools to reach for when something on this surface looks wrong.
14. Four to eight labs that walk you through every pattern hands-on.
15. Challenges that stretch the patterns into realistic scenarios.
16. A field guide to the pitfalls that waste time if you hit them cold.

Follow the chapter end to end if this is your first time. If you are revising, you can approach each section on its own; the structure is designed to read as a complete survey, not only as a linear tutorial.



## What a Device File Actually Is

Long before FreeBSD existed, UNIX committed to a famous idea: **treat devices as files**. A serial line, a disk, a terminal, a stream of random bytes, each of these could be opened, read, written, and closed with the same handful of system calls. User programs did not need to know whether the bytes they consumed came from a spinning disk, a memory buffer, or an imaginary source like `/dev/null`.

That idea was not marketing. It was a design choice Ken Thompson and Dennis Ritchie made in the first versions of UNIX in the early 1970s, and it shaped the entire operating system that followed. By presenting every device through the same system-call interface as every regular file, they turned every command-line tool that dealt with files into a tool that could also deal with devices. `cat` could copy bytes out of a serial port. `dd` could read from a tape drive. `cp` could stream a stream. That alignment is still the single most useful property of a UNIX-like system, and FreeBSD inherits it in full.

FreeBSD keeps that spirit. From user space, a device file looks like any other entry in the filesystem. It has a path, an owner, a group, a mode, and a type visible to `ls -l`. You can `open(2)` it, pass the returned file descriptor to `read(2)` and `write(2)`, query it with `stat(2)`, and close it when you are done.

### A Short History, in Pieces Worth Knowing

The device-file model you will be writing into has gone through a few serious revisions since 1971, and each revision was driven by a real limitation of the previous one. Knowing the broad strokes saves confusion later when you read old books or old manual pages.

In **V7 UNIX** and the BSDs that followed for two decades, the entries under `/dev` were real entries on a real on-disk filesystem. An administrator used `mknod(8)` to create them, passing a device type (character or block) and a pair of small integers called the *major number* and the *minor number*. The kernel used the major number to pick a row in a table of drivers (the `cdevsw` or `bdevsw` array, depending on type), and the minor number to pick which instance of that driver the call was going to. The pair was typed into `/dev` once, by hand or by a shell script called `MAKEDEV`, and then lived on the disk forever.

That model worked for a long time. It stopped working when two things happened at once: hardware started being hot-pluggable, and the space of major numbers got crowded enough that every kernel change required coordination across the tree. A disk plugged in at runtime needed a node that had not existed before boot. A driver that moved from one tree location to another needed its numbers renegotiated. Static `/dev` entries misrepresented both.

In **FreeBSD 5**, released in 2003, the answer was **devfs**, a kernel-managed virtual filesystem that replaces the on-disk `/dev` entirely. When a driver creates a device node through `make_dev(9)`, devfs adds an entry to its live tree and user programs can see it immediately. When the driver calls `destroy_dev(9)`, devfs removes the entry. Major and minor numbers still exist inside the kernel as an implementation detail, but they are not part of the contract any more. Paths and cdev pointers are. That is the model you are writing into today.

A third shift worth naming: **block devices have retreated from userland visibility**. Older UNIX variants exposed certain storage devices as block-special files whose type letter in `ls -l` was `b`. FreeBSD has not shipped block-special device nodes to userland for many years. Storage drivers still exist in the kernel; they just publish themselves through GEOM and show up in `/dev` as character devices. The only time you will see a `b` in FreeBSD is on disks mounted from other filesystem types. Your drivers will expose character devices, full stop.

### Why the File Abstraction Earned Its Keep

The payoff of the "everything is a file" idea is that every tool in the base system is, without special knowledge, already a tool for talking to your device. That is worth a minute of reflection because it sets the tone for how you should design your driver.

`cat` reads from files. It will also read from `/dev/myfirst/0` once your driver implements `d_read`, with no special build and no awareness that it is talking to a driver. `dd` reads and writes files in arbitrarily sized blocks; it will happily stream to or from a character device, and it offers flags (`bs=`, `count=`, `iflag=nonblock`) that let operators exercise driver behavior without writing new programs. `tee`, `head`, `tail` in follow mode, `od`, `hexdump`, `strings`, all of them already work on your device the day you ship it. Shell redirection works. Pipes work. The kernel's file-descriptor machinery does not care which side of a pipe is a device and which side is a regular file.

The design guidance that flows from this is simple and strict: **your driver should behave like a file whenever it can**. That means returning `read(2)` lengths that match reality, returning end-of-file as zero bytes read, returning `write(2)` results that count the bytes actually consumed, respecting `errno` conventions when something goes wrong, and not inventing new meanings for existing system calls unless the meaning is unavoidable. The more your device looks like an ordinary file to every tool in `/bin` and `/usr/bin`, the less your users have to learn, and the less brittle your interface becomes when new tools appear years from now.

### What a Device File Is Not

The abstraction is generous but it is not unlimited. It is worth naming a few things a device file is explicitly not, so you do not design around the wrong mental model.

A device file is **not an IPC channel** in general. It can behave like one, the same way a named pipe can behave like one, but the classic tools for interprocess communication are `pipe(2)`, `socketpair(2)`, UNIX-domain sockets, and `kqueue(9)`. If two user programs want to exchange messages with each other, they should use those tools rather than opening a device node as a side channel. A driver that lets itself be pressed into service as an ad-hoc IPC bus will find its semantics getting more and more complicated as users invent new uses for it.

A device file is **not a registry of settings** your driver keeps across reboots. devfs does not persist anything. Anything you write into `/dev/yournode` is processed by your driver at the moment it is written and is gone unless your driver chose to remember it. If you need persistent configuration, the right tools are `sysctl(8)` tunables, loader environment variables set via `loader.conf(5)`, and configuration files the userland part of your stack reads from `/etc`.

A device file is **not a broadcast medium** by default. If a driver wants to deliver the same byte stream to every open file descriptor, it must implement that explicitly; the kernel does not fan writes across readers automatically, and it does not duplicate reads into multiple files. Chapter 10's discussion of `poll(2)` and `kqueue(9)` touches the edge of this, and several drivers in the tree (for example `/usr/src/sys/net/bpf.c`) solve it on purpose. It is not free.

A device file is **not a substitute for a system call**. If your driver needs a structured, typed, versioned control interface that user programs invoke with named commands, that is what `ioctl(2)` is for. We do not design `ioctl` in this chapter, but the distinction matters today: do not smuggle control commands through `write(2)` strings when `ioctl(2)` would express them more precisely. Chapter 25 revisits `ioctl` design as part of advanced driver practice.

Holding those limits in mind now will keep your design honest later. The device-file surface is a small number of well-shaped primitives. The art is choosing which of them your driver exposes, and wiring each of them carefully.

### The Variety You Will See Under /dev

A short tour of a freshly booted FreeBSD 14.3 system is a good way to calibrate what this chapter is shaping. Open a terminal and run `ls /dev` on your lab machine. You will see a cross-section of the naming patterns the book will teach you to recognise:

- **Singletons**: `null`, `zero`, `random`, `urandom`, `full`, `klog`, `mem`, `kmem`.
- **Numbered instances**: `bpf0`, `bpf1`, `md0`, `ttyu0`, `ttyv0`, `ttyv1`, `cuaU0`.
- **Subdirectories per driver**: `led/`, `pts/`, `fd/`, `input/`.
- **Standard names with special meaning**: `stdin`, `stdout`, `stderr`, `console`, `tty`.
- **Aliases for conventional paths**: `log`, `sndstat`, `midistat`.

Every entry in that tour is a cdev that some driver or subsystem asked devfs to present. Some were created by drivers that loaded at boot, some by drivers that were compiled into the kernel itself, and some by the devfs mount's own setup. Each of them was shaped by the same decisions you are about to make for `myfirst`.

Take a quick look at the nodes already present on your lab system:

```sh
% ls -l /dev/null /dev/zero /dev/random
crw-rw-rw-  1 root  wheel     0x17 Apr 17 09:14 /dev/null
crw-r--r--  1 root  wheel     0x14 Apr 17 09:14 /dev/random
crw-rw-rw-  1 root  wheel     0x18 Apr 17 09:14 /dev/zero
```

The leading `c` in each mode field tells you these are **character devices**. On FreeBSD there are no block-device nodes to worry about in this context; storage is served through character devices fronted by GEOM, and the old `block special` distinction is no longer exposed to userland the way it was in older UNIXes. For the work you are doing now, and for most drivers you will write, **character device** is the shape your node will take.

The interesting part of `ls -l` is what is *not* there. There is no backing file on disk that holds the bytes of `/dev/null`. There is no regular file hiding behind `/dev/random` somewhere under `/var`. These nodes are presented by the kernel on demand, and the permissions and ownership you see are whatever the kernel has chosen to advertise. That is the key mental shift for this chapter: in FreeBSD, the entries under `/dev` are not ordinary files that your driver pokes at. They are a **view** of kernel-side objects called `struct cdev`, served by a dedicated filesystem.

### Character Devices Are the Common Case

A character device delivers a stream of bytes through `read(2)` and accepts a stream of bytes through `write(2)`. It may or may not support seeking. It may support `ioctl(2)`, `mmap(2)`, `poll(2)`, and `kqueue(9)`, and each of those is opt-in on the driver side. The driver declares which operations it supports by filling in the relevant fields of a `struct cdevsw`, exactly as you saw in Chapter 7.

`myfirst` is a character device. So are `/dev/null`, `/dev/zero`, the terminal nodes under `/dev/ttyu*`, the BPF packet interfaces under `/dev/bpf*`, and many others. When you are new to driver work, "character device" is almost always the right answer.

If you are coming from a background where block-special nodes were common, the mental adjustment is small. On FreeBSD you never write a driver that exposes a block-special node directly; storage drivers produce character devices that GEOM consumes, and GEOM in turn republishes its own character devices upward. For this chapter and the next several, the phrase "device file" always means a character device.

### Reading `ls -l` for a Device Node

It is worth spending a minute on the shape of the output, because every line you will ever inspect with `ls -l` follows the same template.

```sh
% ls -l /dev/null
crw-rw-rw-  1 root  wheel     0x17 Apr 17 09:14 /dev/null
```

The `c` in the leading position says this is a character device. A regular file would show `-`, a directory would show `d`, a symbolic link would show `l`. The nine permission characters after it read exactly the same way as for a regular file: three triples of owner, group, and other permissions, in the order read, write, execute. Devices ignore the execute bit, so you will almost never see it set.

The owner is `root` and the group is `wheel`. Those are printed from the values the kernel advertises, which is a mix of whatever the driver asked for and whatever `devfs.conf` or `devfs.rules` applied on top. Change any of them and this column changes immediately; there is no file on disk to rewrite.

The field that looks odd is `0x17`. On a regular file this column holds the size in bytes. On a device file, devfs reports a small hexadecimal identifier instead. It is not a major or minor number in the old System V sense, and you will not normally need to interpret it. If you want to confirm two names point at the same underlying cdev (for instance a primary node and an alias), `stat -f '%d %i' path` is a more reliable way to compare. We will come back to it in the userland section.

Finally, `ls -l` on a directory under `/dev` works exactly as you would expect, because devfs really is a filesystem. `ls -l /dev/myfirst` will list the files inside the `myfirst/` subdirectory if the driver placed one there.



## devfs: Where /dev Comes From

On an old-school UNIX system, `/dev` was a real directory on a real disk, and device nodes were created by a command called `mknod(8)`. If you needed a new node, you ran `mknod` with a type, a major number, and a minor number, and an entry appeared on disk. It was static. It did not care whether the hardware was present. It did not clean up after itself.

FreeBSD moved away from that model. On a modern FreeBSD system, `/dev` is a **devfs** mount, a virtual filesystem maintained entirely by the kernel. You can see it in the output of `mount(8)`:

```sh
% mount | grep devfs
devfs on /dev (devfs)
```

Inside a jail, you will usually see a second devfs mounted at the jail's own `/dev`. The same filesystem type, the same mechanism, just a different view filtered by `devfs.rules`.

The rules of devfs are simple, and they are worth internalizing:

1. You do not create files under `/dev` with `touch` or `mknod`. You create them from inside the kernel, by calling `make_dev_s(9)` or one of its relatives.
2. When your driver's cdev goes away, the corresponding entry disappears from `/dev` automatically.
3. Ownership, group, and mode on a node start out with whatever your driver asked for, and can be adjusted from userland through `devfs.conf` and `devfs.rules`.
4. Nothing about a devfs node is persisted between reboots. It is always a live reflection of the current state of the kernel.

That last point is the one that catches people by surprise the first time. You cannot `chmod /dev/myfirst0` and expect the change to survive the next reboot. If you need a permission to stick, you encode it either in the driver or in one of the devfs configuration files. We will do both in this chapter.

If you want to look at devfs directly, its implementation lives in `/usr/src/sys/fs/devfs/`. You do not need to read it cover to cover, but being aware of where it is will save you confusion later when someone asks why `/dev/foo` looks the way it does.

### Why This Model Replaced `mknod`

The move away from static device nodes in on-disk `/dev` was not a stylistic choice. It was driven by three real problems:

1. **Nodes for hardware that was not present.** Before devfs, `/dev` carried entries for every device the system *could* have, whether the kernel currently had a driver for it or not. Users were left guessing which paths were live.
2. **Stale nodes for hardware that had been removed.** Hot-pluggable hardware (USB, FireWire, CardBus back in its day) made static `/dev` trees actively misleading.
3. **Major and minor number exhaustion.** The pair `(major, minor)` was a finite resource and a source of allocation politics across the kernel tree. devfs sidesteps that problem entirely.

Today `/dev` is a running mirror of what the kernel actually supports and what devices are currently present. A driver that loads makes a node; a driver that unloads retracts it. A disk that is removed vanishes. That is by design, and it is why the word "node" is a better mental model than "file" for the things you see under `/dev`.

### What devfs Is and Is Not

devfs **is** a filesystem. You can `ls` inside it, change into subdirectories, redirect into nodes, and so on. What devfs **is not** is a general place to keep user data. Do not try to `echo` a log file into `/dev`. Do not expect `touch` to work inside it. devfs accepts a small, well-defined set of operations, and anything outside that set returns an error.

This narrow surface is a feature. It means devfs never surprises you by persisting state, never competes with a regular filesystem for space, and never misinterprets a regular file as a device. Your driver creates nodes; devfs presents them; everything else goes through your `cdevsw` handlers.

### devfs in the Family of Synthetic Filesystems

FreeBSD has a small family of filesystems that do not store files on any disk. devfs is one of them. Others you may have met are `tmpfs(5)`, which serves files from RAM, `nullfs(5)`, which republishes another directory under a new name, and `fdescfs(5)`, which presents each process's file descriptors as files under `/dev/fd`. They are all real filesystems in the eyes of `mount(8)` and the VFS layer, but each one synthesises its contents from something other than a block device.

Knowing the family helps for two reasons. The first is that devfs shares a few ideas with its relatives. All synthetic filesystems lay their tree on demand, they all manage their storage outside any on-disk container, and they all have strong opinions about which operations do and do not make sense inside them. The second reason is that you will see them combined in practice. A jail typically mounts its own devfs under its own `/dev`, and it may also mount a `nullfs` view of `/usr/ports` or a `tmpfs` for `/var/run`. Reading the output of `mount(8)` inside a running FreeBSD host or jail is the quickest way to get a feel for how these filesystems cooperate.

A quick look on a typical lab host might show:

```sh
% mount
/dev/ada0p2 on / (ufs, local, journaled soft-updates)
devfs on /dev (devfs)
fdescfs on /dev/fd (fdescfs)
tmpfs on /tmp (tmpfs, local)
```

Each of those is a filesystem type with its own semantics. devfs is the one we care about in this chapter, and its job description is unique: present the kernel's live collection of `struct cdev` objects as a tree of file-like nodes.

### Mount Options Worth Knowing

devfs accepts a handful of mount options, defined in the devfs code and described in the manual page for `mount_devfs(8)`. You will not often change them from the defaults, but it helps to recognise them when you see them in `/etc/fstab`, in jail configurations, or in the output of `mount -v`.

- **`ruleset=N`**: applies devfs ruleset `N` to the mount. A ruleset is a named list of path patterns and actions defined in `/etc/devfs.rules`. This option is how jails limit what their `/dev` looks like.
- **`noauto`**: present in `fstab` to mark the filesystem as not mounted automatically at boot.
- **`late`**: mounts late in the boot sequence, after local filesystems and network. Relevant when combined with `ruleset=`.

The typical host configuration needs none of these; the default devfs mount at `/dev` is plain. Where they matter most is in jail configuration, which is why Section 10 of this chapter walks through a complete jail example.

### devfs Inside Jails

A FreeBSD jail is a restricted execution environment with its own filesystem view, its own set of processes, and usually its own `/dev`. When `jail(8)` starts a jail with `mount.devfs=1` in its configuration, it mounts a separate devfs under the jail's `/dev`. That mount is an instance of the same filesystem type, with one decisive difference: it applies a ruleset that filters what appears inside the jail.

The default ruleset for a jail is `devfsrules_jail`, numbered `4` in `/etc/defaults/devfs.rules`. That is the path readers will actually edit or consult on a running system; the source home from which it is installed is `/usr/src/sbin/devfs/devfs.rules`, for readers who want to see the canonical shipped rules. It starts from `devfsrules_hide_all` (which hides everything) and then selectively unhides exactly the handful of nodes a typical jail needs: `null`, `zero`, `random`, `urandom`, `crypto`, `ptmx`, `pts`, `fd` and the PTY nodes. Everything else that exists on the host's `/dev` simply does not exist inside the jail. The jail cannot open it, cannot list it, cannot stat it.

This is the mechanism that keeps jails honest. It is also the mechanism you will interact with if your driver's nodes should, or should not, appear inside a jail. If a lab needs `/dev/myfirst/0` to be reachable from a jail, you write a ruleset that unhides it. If a deployment should keep the node out of jails by default, you do nothing: devfs will hide it for you. Chapter 8 revisits this in detail when we reach Section 10.

### chroot and devfs

There is one related context worth a short note because it sometimes confuses people reading older documentation. A plain `chroot(8)` environment does **not** automatically get its own devfs. If a shell script chroots into `/compat/linux` and then tries to open `/dev/null`, the open succeeds only because `/dev/null` exists on the host and is visible under the chroot's path through a bind mount or because a devfs was mounted there explicitly. If neither is true, the open fails with `ENOENT`.

Jails are different because jails explicitly build a filesystem view with devfs mounted inside. A chroot is lower-level and leaves filesystem arrangement entirely to the caller. The practical consequence, for driver authors, is that a regression test that runs under `chroot` may or may not be able to see your device, depending on how the chroot was set up. When in doubt, test inside a jail.

### /dev/fd and the Standard Symlinks

A few entries under `/dev` are not cdevs at all; they are symbolic links maintained as part of the devfs mount. `/dev/stdin`, `/dev/stdout`, `/dev/stderr` each resolve through `/dev/fd/0`, `/dev/fd/1`, and `/dev/fd/2`, and those in turn are served by `fdescfs(5)` when it is mounted on `/dev/fd`. The combination gives user programs a reliable path-based way to reference the current file descriptor, which is occasionally useful in shell scripts and awk programs that want to read or write "whatever the current program's stdin is".

These entries are worth a mention for two reasons. First, they are examples of symlinks inside devfs: `ls -l /dev/stdin` shows `lrwxr-xr-x` and an arrow, not `crw-...`. Second, they are a reminder that the entries under `/dev` are not all cdevs. Most are; a few are not. When the chapter later contrasts `make_dev_alias(9)` with the `link` directive in `devfs.conf`, this is the distinction that sits underneath.

### Why the "Live Mirror" Property Matters to Drivers

The fact that devfs is a live mirror of the kernel's current state has several implications for how you design and debug drivers. Each of these points will come back in the chapter; it helps to state them plainly now.

- **A node that is missing from `/dev` is a node your driver did not create.** If you expected `/dev/myfirst/0` to exist and it does not, the first thing to check is whether `attach` ran and whether `make_dev_s` returned zero. `dmesg` usually tells you.
- **A node that lingers after unload is a node your driver did not destroy.** That cannot happen if you used `destroy_dev(9)` correctly, but it is a useful frame: if the path survives the `kldunload`, you missed a call.
- **A permission change you made interactively will not survive a reboot.** devfs has no on-disk record of what you did. The persistent tools for expressing policy are `devfs.conf` and `devfs.rules`, and Section 10 of this chapter covers them in depth.
- **A jail sees a filtered subset.** Assume, when reasoning about security or feature exposure, that someone is eventually going to run your driver with a jail active on the same host. If your node is too loose and a ruleset lets jails see it, the loose mode has a larger blast radius.



## cdev, vnode, and File Descriptor

Open a device file and three kernel-side objects quietly line up behind your file descriptor. Understanding this trio is the difference between writing a driver that just happens to work and a driver whose lifecycle you actually control.

The first object is `struct cdev`, the **kernel-side identity of the device**. There is one `struct cdev` per device node, no matter how many programs have it open. Your driver creates it with `make_dev_s(9)` and destroys it with `destroy_dev(9)`. The cdev carries the identifying information about the node: its name, its owner, its mode, the `struct cdevsw` that dispatches system calls, and two driver-controlled slots called `si_drv1` and `si_drv2`. Chapter 7 already used `si_drv1` to stash the softc pointer, and that is by far the most common use for it.

The second object is a **devfs vnode**. Vnodes are the generic FreeBSD VFS objects that represent open filesystem inodes. Each device node has a vnode underneath it, just as a regular file does, and the VFS layer uses the vnode to route operations toward the correct filesystem. For a device node, that filesystem is devfs, and devfs forwards the operation to the cdev.

The third object is the **file descriptor** itself, represented inside the kernel by a `struct file`. Unlike the cdev, there is one `struct file` per open, not one per device. This is where per-open state lives. Two processes that both open `/dev/myfirst0` share the same cdev but get separate file structures, and devfs knows how to keep those structures cleanly distinct.

Put the three together and the path of a single `read(2)` looks like this:

```text
user process
   read(fd, buf, n)
         |
         v
 file descriptor (struct file)  per-open state
         |
         v
 devfs vnode                     VFS routing
         |
         v
 struct cdev                     device identity
         |
         v
 cdevsw->d_read                  your driver's handler
```

Every box above exists independently, and each one has a different lifetime. The cdev lives for as long as your driver keeps it alive. The vnode lives for as long as anyone has the node resolved on the VFS layer. The `struct file` lives for as long as the user process keeps its descriptor open. When you write a driver, you are filling in only the last row of that diagram, but it helps enormously to know the rows above.

### Tracing a Single read(2) Through the Stack

Walk through the story once in prose, with a concrete `read(2)` call as the anchor. A user program has this line:

```c
ssize_t n = read(fd, buf, 64);
```

Here is what happens. The kernel receives the `read(2)` syscall and looks up `fd` in the calling process's file-descriptor table. That yields a `struct file`. The kernel sees that the file's type is a vnode-backed file whose vnode lives in devfs, so it dispatches through the generic file-operation vector into devfs's read handler.

devfs takes a reference on the underlying `struct cdev`, retrieves the pointer to `struct cdevsw` from it, and calls `cdevsw->d_read`. That is **your** function. Inside it you inspect the `struct uio` the kernel prepared, look at the device through the `struct cdev *dev` argument, and optionally recover the per-open structure with `devfs_get_cdevpriv`. When you return, devfs releases its reference on the cdev and the read call unwinds back to the user program.

A few invariants fall out of that trace and are worth remembering:

- **Your handler never runs if the cdev is gone.** Between `destroy_dev(9)` retiring the node and the last caller dropping its reference, devfs simply refuses new operations.
- **Two calls from two processes can reach `d_read` simultaneously.** Neither devfs nor the VFS layer serialises callers on your behalf. Concurrency control is your responsibility, and Part 3 of this book is dedicated to it.
- **The `struct file` you are implicitly serving is hidden from your handler.** You do not need to know which descriptor triggered the call; you only need the cdev, the uio, and (optionally) the cdevpriv pointer.

That last point is the one that earns its keep in practice. By hiding the descriptor from the handler, FreeBSD gives you a clean API: all the per-descriptor bookkeeping goes through `devfs_set_cdevpriv` and `devfs_get_cdevpriv`, and your handler code stays small.

### Why This Matters for Beginners

Two practical consequences fall out of this model, and both will come back in the next chapter.

First, **pointers stored on the cdev are shared across all opens**. If you store a counter in `si_drv1`, every process that opens the node sees the same counter. That is perfect for driver-wide state such as the softc, and terrible for per-session state such as a read position.

Second, **the kernel does not care how many times your device is opened**. Unless you tell it otherwise, every `open(2)` just goes through. If you need exclusive access, as the Chapter 7 code does through its `is_open` flag, you have to enforce it yourself. If you need per-open bookkeeping, you attach that bookkeeping to the file descriptor, not to the cdev. We will do both before the end of the chapter.

### A Closer Look at struct cdev

You have been using `struct cdev` through a pointer for the whole of Chapter 7. It is time to look inside it. The full definition is in `/usr/src/sys/sys/conf.h`, and the important fields are these:

```c
struct cdev {
        void            *si_spare0;
        u_int            si_flags;
        struct timespec  si_atime, si_ctime, si_mtime;
        uid_t            si_uid;
        gid_t            si_gid;
        mode_t           si_mode;
        struct ucred    *si_cred;
        int              si_drv0;
        int              si_refcount;
        LIST_ENTRY(cdev) si_list;
        LIST_ENTRY(cdev) si_clone;
        LIST_HEAD(, cdev) si_children;
        LIST_ENTRY(cdev) si_siblings;
        struct cdev     *si_parent;
        struct mount    *si_mountpt;
        void            *si_drv1, *si_drv2;
        struct cdevsw   *si_devsw;
        int              si_iosize_max;
        u_long           si_usecount;
        u_long           si_threadcount;
        union { ... }    __si_u;
        char             si_name[SPECNAMELEN + 1];
};
```

Not every field matters for a beginner-level driver. A few do, and knowing what they represent saves hours the first time you look at unfamiliar code.

**`si_name`** is the null-terminated name of the node as devfs sees it. When you pass `"myfirst/%d"` and unit `0` to `make_dev_s`, this is the field that ends up containing the string `myfirst/0`. The helper `devtoname(struct cdev *dev)` returns a pointer to this field and is the right tool for logging or debug output.

**`si_flags`** is a bit field that carries status flags about the cdev. The flags your driver will touch most often are `SI_NAMED` (set when `make_dev*` has placed the node into devfs) and `SI_ALIAS` (set on aliases created by `make_dev_alias`). The kernel manages them; your code rarely, if ever, writes to this field directly. A useful reading habit: if you see an unfamiliar `SI_*` flag in someone else's driver, look it up in `/usr/src/sys/sys/conf.h` and read the one-line comment.

**`si_drv1`** and **`si_drv2`** are the two generic driver-controlled slots. Chapter 7 used `si_drv1` to stash the softc pointer, and that is the most common pattern. `si_drv2` is available for a second pointer when you need one. These fields are yours to use; the kernel never touches them.

**`si_devsw`** is the pointer to the `struct cdevsw` that dispatches operations on this cdev. It is the link between the node and your handlers.

**`si_uid`**, **`si_gid`**, **`si_mode`** hold the advertised ownership and mode. They are set from the `mda_uid`, `mda_gid`, `mda_mode` arguments you pass to `make_dev_args_init`. They are mutable in principle, but the right way to change them is through `devfs.conf` or `devfs.rules`, not by assigning into the struct.

**`si_refcount`**, **`si_usecount`**, **`si_threadcount`** are the three counters devfs uses to keep the cdev alive while anyone might still touch it. `si_refcount` counts long-lived references (the cdev is listed in devfs, other cdevs may alias it). `si_usecount` counts active open file descriptors. `si_threadcount` counts kernel threads currently executing inside a `cdevsw` handler for this cdev. Your driver almost never reads these directly; the routines `dev_ref`, `dev_rel`, `dev_refthread`, and `dev_relthread` manage them on your behalf. What matters conceptually is that `destroy_dev(9)` will refuse to finish tearing a cdev down while `si_threadcount` is non-zero; it waits, sleeping briefly, until every in-flight handler has returned.

**`si_parent`** and **`si_children`** link a cdev into a parent-child relationship. This is how `make_dev_alias(9)` wires an alias cdev to its primary and how certain clone mechanisms wire per-open nodes to their template. Most of the time you will not interact with these fields; it is enough to know they exist and that they are one of the reasons devfs can unwind an alias cleanly when the primary is destroyed.

**`si_flags & SI_ETERNAL`** deserves a short note. Some nodes, in particular the ones `null` creates for `/dev/null`, `/dev/zero`, and `/dev/full`, are flagged as eternal with `MAKEDEV_ETERNAL_KLD`. The kernel refuses to destroy them during normal operation. When you start writing modules that expose devices at KLD load time and want the nodes to stay alive across unload attempts, this is the knob. For a driver in active development, leave it alone.

### struct cdevsw: The Dispatch Table

Your Chapter 7 `cdevsw` filled in a handful of fields. The real structure is longer, and the remaining fields are worth at least a recognition pass, because you will meet them in real drivers and sooner or later want to use some of them.

The structure is defined in `/usr/src/sys/sys/conf.h` as:

```c
struct cdevsw {
        int              d_version;
        u_int            d_flags;
        const char      *d_name;
        d_open_t        *d_open;
        d_fdopen_t      *d_fdopen;
        d_close_t       *d_close;
        d_read_t        *d_read;
        d_write_t       *d_write;
        d_ioctl_t       *d_ioctl;
        d_poll_t        *d_poll;
        d_mmap_t        *d_mmap;
        d_strategy_t    *d_strategy;
        void            *d_spare0;
        d_kqfilter_t    *d_kqfilter;
        d_purge_t       *d_purge;
        d_mmap_single_t *d_mmap_single;
        /* fields managed by the kernel, not touched by drivers */
};
```

Take the fields one at a time.

**`d_version`** is an ABI stamp. It must be set to `D_VERSION`, a value defined a few lines above the structure. The kernel checks this field when registering the cdevsw and will refuse to proceed if the stamp does not match. Forgetting to set it is a classic beginner bug: the driver compiles, loads, and then either produces weird errors on first open or crashes the system outright. Always set `d_version = D_VERSION` as the first field in every `cdevsw` you write.

**`d_flags`** carries a set of cdevsw-wide flags. The flag names are defined with the rest of the structure. The ones worth recognising now are:

- `D_TAPE`, `D_DISK`, `D_TTY`, `D_MEM`: hint to the kernel about the nature of the device. For most drivers you leave this zero.
- `D_TRACKCLOSE`: if set, devfs calls your `d_close` for every `close(2)` on a descriptor, not only for the last close. Useful when you want to reliably run per-descriptor teardown even in the face of `dup(2)`.
- `D_MMAP_ANON`: special handling for anonymous memory mappings. `/dev/zero` sets this, which is how `mmap(..., /dev/zero, ...)` yields zero-filled pages.
- `D_NEEDGIANT`: forces dispatch of this cdevsw's handlers under the Giant lock. Modern drivers should not need this; if you see it in code, treat it as a historical marker rather than a model to follow.
- `D_NEEDMINOR`: signals that the driver uses `clone_create` to allocate minor numbers for cloned cdevs. You will not need this in Chapter 8.

**`d_name`** is the base name string used by the kernel when logging about this cdevsw. It also becomes part of the pattern the `clone_create(9)` mechanism uses when it synthesises cloned devices. Set it to a short, human-readable string such as `"myfirst"`.

**`d_open`**, **`d_close`**: session boundaries. Called when a user program calls `open(2)` on the node or releases its last descriptor with `close(2)`. Chapter 7 introduced both, and this chapter refines how you use them.

**`d_fdopen`**: an alternative to `d_open` for drivers that want the `struct file *` passed to them directly. Rare in beginner-level drivers. Ignore unless a future chapter introduces it.

**`d_read`**, **`d_write`**: byte-stream operations. Chapter 7 left these stubbed. Chapter 9 implements them with `uiomove(9)`.

**`d_ioctl`**: control-path operations. Chapter 25 will treat `ioctl` design in depth. For now, recognise the field and know that it is where structured commands from `ioctl(2)` land.

**`d_poll`**: called by `poll(2)` to ask whether the device is currently readable or writable. Chapter 10 handles this as part of the I/O-efficiency story.

**`d_kqfilter`**: called by `kqueue(9)` machinery. Same chapter.

**`d_mmap`**, **`d_mmap_single`**: supports mapping the device into a user process's address space. Rare in beginner drivers, covered later when it matters.

**`d_strategy`**: called by some kernel layers (notably the old `physio(9)` path) to hand the driver a block of I/O as a `struct bio`. Not relevant for the pseudo-devices you will be writing in Part 2.

**`d_purge`**: called by devfs during destruction if the cdev still has threads running inside its handlers. A well-written `d_purge` wakes those threads and convinces them to return quickly so destruction can proceed. Most simple drivers do not need one; Chapter 10 revisits this in the context of blocking I/O.

When you design your own cdevsw, you fill in only the fields that correspond to operations your device actually supports. Every `NULL` field is a polite refusal: the kernel interprets it either as "this operation is not supported on this device" or as "use the default behavior", depending on which operation it is. Do not reach into the spare fields.

### The D_VERSION Stamp and Why It Exists

A short aside about `d_version` is useful, because it will save you time the first time your driver mysteriously fails to register.

The kernel interface for cdevsw structures has evolved over FreeBSD's lifetime. Fields have been added, removed, or changed type across major releases. The `d_version` stamp is the kernel's way of confirming that your module was built against a compatible definition of the structure. The canonical way to set it is:

```c
static struct cdevsw myfirst_cdevsw = {
        .d_version = D_VERSION,
        /* ...remaining fields... */
};
```

The macro `D_VERSION` is defined in `/usr/src/sys/sys/conf.h` and will be updated by the kernel team whenever the structure changes in a way that breaks ABI. Modules built against the new header get the new stamp. Modules built against an old header get an old stamp and the kernel refuses them.

This is a small detail that saves large headaches. Set it every time. If you ever see the kernel print a cdevsw version mismatch on load, your build environment and your running kernel have drifted; rebuild the module against the headers of the kernel you intend to run.

### Reference Counting at the cdev Level

The counters you saw on `struct cdev` are the engine that keeps device destruction safe. A simple way to picture them:

- `si_refcount` is the "how many things in the kernel still hold this cdev by the neck" count. Aliases, clones, and certain bookkeeping paths bump it. The cdev cannot actually be freed while this is non-zero.
- `si_usecount` is the "how many user-space file descriptors have this cdev open" count. It is incremented by devfs on a successful `open(2)` and decremented on `close(2)`. Your driver never touches it directly.
- `si_threadcount` is the "how many kernel threads are right now executing inside one of my `cdevsw` handlers" count. It is incremented by `dev_refthread(9)` when devfs enters a handler on your behalf and decremented by `dev_relthread(9)` when the handler returns. Your driver never touches it directly.

The rule that makes this usable is: `destroy_dev(9)` will block until `si_threadcount` falls to zero and will not return until no more handlers can be entered for this cdev. That is how `destroy_dev` is able to guarantee that after it returns, your handlers will not be called again. The section later in this chapter titled "Destroying cdevs Safely" revisits this guarantee and the cases where you need its stronger cousin `destroy_dev_drain(9)`.

### One More Turn on Lifetimes

With that in hand, the diagram from the previous subsection has a little more meaning than it did the first time. The cdev is a long-lived kernel object whose lifetime is under your driver's control. The vnode is a VFS-layer object that lives only as long as the filesystem layer has use for it. The `struct file` is a short-lived per-open object that lives only as long as the process keeps the descriptor. And underneath all three, the counters described above keep them honest.

You do not need to memorise any of that. You need to recognise the shape. When you later read a driver and see `dev_refthread` or `si_refcount`, you will remember what they are for. When you watch `destroy_dev` sleep in a debugger, you will recognise that it is waiting for `si_threadcount` to drop. That recognition is what turns kernel code from a puzzle into something you can reason about.



## Permissions, Ownership, and Mode

When your driver calls `make_dev_s(9)`, three fields on the `struct make_dev_args` decide what `ls -l /dev/yournode` will show:

```c
args.mda_uid  = UID_ROOT;
args.mda_gid  = GID_WHEEL;
args.mda_mode = 0600;
```

`UID_ROOT`, `UID_BIN`, `UID_UUCP`, `UID_NOBODY`, `GID_WHEEL`, `GID_KMEM`, `GID_TTY`, `GID_OPERATOR`, `GID_DIALER`, and a handful of related names are defined in `/usr/src/sys/sys/conf.h`. Use those constants rather than raw numbers when a well-known identity exists. It makes your driver easier to read and it protects you from silent drift if the numeric value ever changes.

The mode is a classic UNIX permission triple. The meaning of each bit is the same as for a regular file, with the caveat that devices do not care about the execute bit. A few combinations come up often:

- `0600`: owner read and write. The safest default for a driver that is still being developed.
- `0660`: owner and group read and write. Appropriate when you have a well-defined privileged group, such as `operator` or `dialer`.
- `0644`: owner read and write, everyone can read. Rare for control devices, sometimes appropriate for read-only status or random-byte style nodes.
- `0666`: everyone can read and write. Used only for intentionally harmless sources like `/dev/null` and `/dev/zero`. Do not reach for this unless you have a real reason.

The rule of thumb is simple: ask "who actually needs to touch this node?" and encode that answer, no more. Opening permissions wider later is easy. Narrowing them after users have come to depend on the wider mode is not.

### Where the Mode Comes From

It is worth being explicit about who decides the final mode on the node. Three actors have a say:

1. **Your driver**, through the `mda_uid`, `mda_gid`, and `mda_mode` fields at `make_dev_s()` time. This is the baseline.
2. **`/etc/devfs.conf`**, which can apply a one-time static adjustment when a node appears. This is the standard way an operator tightens or loosens permissions on a specific path.
3. **`/etc/devfs.rules`**, which can apply rule-based adjustments, commonly to filter what a jail sees.

If the driver sets `0600` and nothing else is configured, you will see `0600`. If the driver sets `0600` and `devfs.conf` says `perm myfirst/0 0660`, you will see `0660` for that node. The kernel is the mechanism; the operator's configuration is the policy.

### Named Groups You Will Meet

FreeBSD ships with a small set of well-known groups that show up repeatedly in device ownership. Each has a matching constant in `/usr/src/sys/sys/conf.h`. A brief field guide helps you choose one quickly:

- **`GID_WHEEL`** (`wheel`). Trusted administrators. The safest default when you are not sure who should have access beyond root.
- **`GID_OPERATOR`** (`operator`). Users who run operational tools but are not full administrators. Commonly used for devices that need human supervision but should not require `sudo` every time.
- **`GID_DIALER`** (`dialer`). Historically for serial-port dial-out access. Still used for TTY nodes that user-space dial programs need.
- **`GID_KMEM`** (`kmem`). Read access to kernel memory through `/dev/kmem`-style nodes. Very sensitive, rarely the right answer for a new driver.
- **`GID_TTY`** (`tty`). Ownership for terminal devices.

When a suitable named group exists, use it. When none fits, leave the group as `wheel` and add `devfs.conf` entries for sites that need their own grouping. Inventing a brand-new group in your driver is rarely worth the trouble.

### A Worked Example

Suppose the driver baseline is `UID_ROOT`, `GID_WHEEL`, `0600`, and you want to let a specific lab user read and write through a controlled group. The sequence looks like this.

With the driver loaded and no `devfs.conf` entries:

```sh
% ls -l /dev/myfirst/0
crw-------  1 root  wheel     0x5a Apr 17 10:02 /dev/myfirst/0
```

Add a section to `/etc/devfs.conf`:

```text
own     myfirst/0       root:operator
perm    myfirst/0       0660
```

Apply it and inspect again:

```sh
% sudo service devfs restart
% ls -l /dev/myfirst/0
crw-rw----  1 root  operator  0x5a Apr 17 10:02 /dev/myfirst/0
```

The driver was not reloaded. The cdev in the kernel is the same object. Only the advertised ownership and mode changed, and they changed because a policy file told devfs to change them. This is the layering you want: the driver ships a defensible baseline, and the operator shapes the view.

### Case Studies from the Tree

It helps to spend a minute on the permissions that real FreeBSD devices advertise, because the choices those drivers make are not accidental. Each one is a small design decision, and each one is consistent with the threat model for that kind of node.

`/dev/null` and `/dev/zero` ship with mode `0666`, `root:wheel`. Everyone on the system, privileged or not, is allowed to open them and read or write through them. That is the correct choice because the data they carry is trivially inexhaustible (zero bytes out, bytes-disposed-of in, no hardware state, no secrets). Making them any tighter would break a long list of scripts, tools, and programming idioms that depend on their being universally available. The code that creates them is in `/usr/src/sys/dev/null/null.c`, and the arguments to `make_dev_credf(9)` there are worth a glance.

`/dev/random` is typically mode `0644`, readable by anyone, writable only by root. Read access is deliberately broad because many userland programs need entropy. Write access is narrow because feeding the entropy pool is a privileged operation.

`/dev/mem` and `/dev/kmem` are historically mode `0640`, owner `root`, group `kmem`. The group exists precisely so that privileged monitoring tools can link to them without running as root. The mode is tight because the nodes expose raw memory; a casually readable `/dev/mem` would be a disaster. If you ever see a driver default to a mode this loose for a node that carries hardware state or kernel memory, treat it as a defect.

`/dev/pf`, the control node for the packet filter, is mode `0600`, owner `root`, group `wheel`. A user that can write into `/dev/pf` can change firewall rules. There is no acceptable broader mode; the whole point of the interface is to centralise privileged network configuration, and anything looser would turn the firewall into a free-for-all.

`/dev/bpf*`, the Berkeley Packet Filter nodes, are mode `0600`, owner `root`, group `wheel`. A reader of `/dev/bpf*` sees every packet on an attached interface. That is unambiguous privilege, and the permission reflects it.

TTY nodes under `/dev/ttyu*` and similar hardware-serial surfaces are usually mode `0660`, owner `uucp`, group `dialer`. The `dialer` group exists to let a set of trusted users run dial-out programs without `sudo`. The permission set is the narrowest that still lets the intended workflow function.

The pattern is easy to name: **FreeBSD's base system never chooses wide device permissions unless the data on the other side is harmless**. When you design a node of your own, use that pattern as a mental check. If your node carries data that could hurt someone, narrow the mode. If it carries data that is trivially regenerable and trivially discardable, widening is defensible; do it anyway only when there is a reason.

### Least Privilege Applied to Device Files

"Least privilege" is an overused phrase, but it is precisely right when it applies to device files. You, the driver author, are choosing who can talk to your code from userland, and you get to set the lower bound. Every choice you make wider than necessary is a choice that invites mistakes later.

A practical checklist for each new node you design:

1. **Name the primary consumer in a sentence.** "The monitor daemon reads status every second." "The control tool invokes ioctl to push configuration." "Users of the operator group can read raw packet counters." If you cannot name the consumer, you cannot set permissions; you are guessing.
2. **Derive the mode from the sentence.** A monitor daemon that runs as `root:wheel` and reads once a second wants `0600`. A control tool that a subset of privileged admins run wants `0660` with a dedicated group. A read-only status node consumed by unprivileged dashboards wants `0644`.
3. **Put the reasoning in a comment next to the `mda_mode` line.** Future maintainers will thank you. Future auditors will thank you more.
4. **Default to `UID_ROOT`.** There is almost never a reason for the owner of a node created by a driver to be anything else, unless the driver explicitly models a non-root daemon identity.

The opposite habit, which the book wants to inoculate against, is the "open it up and tighten later" impulse. Permissions on a shipped driver are very hard to tighten, because by the time someone notices, some user's workflow depends on the loose mode and the tightening breaks their day. Start tight. Widen when you have reviewed a real request.

### Transitioning a Loose Mode to a Tight One

Occasionally you will inherit a driver that was wide open and needs to be narrowed. The right approach is three-stage:

**Stage 1: Announce.** Put the planned change in a release note, in the driver's kernel log on first attach, and in whatever operator-facing channel your project uses. Invite feedback for at least one release cycle.

**Stage 2: Offer a transition path.** Either a `devfs.conf` entry that reopens the old mode for people who need it, or a sysctl that the driver reads at attach to pick its default mode. The important property is that a site with a legitimate need to keep the old mode can do so without forking the driver.

**Stage 3: Flip the default.** In the next release after the transition window ends, change the driver's own `mda_mode` to the narrower value. The `devfs.conf` escape hatch remains for sites that need it; everyone else gets the narrower default.

None of that is specific to FreeBSD; it is how any well-run project handles backward-incompatible interface changes. It is worth naming here because device-file permissions have exactly this property: they are part of your driver's public interface.

### What the uid and gid Constants Actually Are

The `UID_*` and `GID_*` constants defined in `/usr/src/sys/sys/conf.h` are **not** guaranteed to match the user and group database on every system. The names chosen in the header correspond to identities that the FreeBSD base system reserves in `/etc/passwd` and `/etc/group`, but a locally modified system could in theory renumber them, or a product built on FreeBSD could add its own. In practice, on every FreeBSD system you will touch, the constants match.

The discipline to hold is simple: use the symbolic name when one exists, and look it up in the header before you invent a new identity. The header currently defines at least these:

- User IDs: `UID_ROOT` (0), `UID_BIN` (3), `UID_UUCP` (66), `UID_NOBODY` (65534).
- Group IDs: `GID_WHEEL` (0), `GID_KMEM` (2), `GID_TTY` (4), `GID_OPERATOR` (5), `GID_BIN` (7), `GID_GAMES` (13), `GID_VIDEO` (44), `GID_RT_PRIO` (47), `GID_ID_PRIO` (48), `GID_DIALER` (68), `GID_NOGROUP` (65533), `GID_NOBODY` (65534).

If you need an identity that is not in the list, the base system probably does not have one reserved. In that case, leave ownership as `UID_ROOT`/`GID_WHEEL` and let operators map your node to their own local group through `devfs.conf`. Inventing a new group inside your driver is almost always the wrong move.

### Three-Layer Policy: Driver, devfs.conf, devfs.rules

When you combine the driver's baseline with `devfs.conf` and `devfs.rules`, you get a layered policy model that is worth seeing once end-to-end. Consider a device that the driver creates with `root:wheel 0600`. Three layers act on it:

- **Layer 1, the driver itself**: sets the baseline. Every `/dev/myfirst/0` on every devfs mount starts at `root:wheel 0600`.
- **Layer 2, `/etc/devfs.conf`**: applied once per host devfs mount, typically at boot. Can change ownership, mode, or add a symlink. On the running host, after `service devfs restart`, the node might appear as `root:operator 0660`.
- **Layer 3, `/etc/devfs.rules`**: applied at mount time based on the ruleset attached to the mount. A jail whose devfs mount uses ruleset `10` sees the filtered, possibly-modified subset. The same node might be hidden inside the jail, or it might be unhidden with further mode and group adjustments.

The practical consequence of this layering is that **the same cdev can look different in different places at the same time**. On the host it might be `0660` owned by `operator`. In a jail it might be `0640` owned by a jailed user identity. In another jail it might not exist at all.

That is a feature, not a bug. It lets you ship a driver with a strict baseline and lets operators widen per-environment without editing your code. Chapter 8 Section 10 walks through all three layers with a worked example.



## Naming, Unit Numbers, and Subdirectories

The printf-style argument to `make_dev_s(9)` chooses where in `/dev` the node appears. In Chapter 7 you used:

```c
error = make_dev_s(&args, &sc->cdev, "myfirst%d", sc->unit);
```

That produced `/dev/myfirst0`. Two details hide in there.

The first detail is `sc->unit`. It is the Newbus unit number that FreeBSD assigns to your device instance. With one instance attached, you get `0`. If your driver supported multiple instances, you might see `myfirst0`, `myfirst1`, and so on.

The second detail is the format string itself. Device names are paths relative to `/dev`, and they may contain slashes. A name such as `"myfirst/%d"` does not produce a weird filename with a slash in it; devfs interprets the slash the way a filesystem does, creates the intermediate directory if needed, and places the node inside. So:

- `"myfirst%d"` with unit `0` gives `/dev/myfirst0`.
- `"myfirst/%d"` with unit `0` gives `/dev/myfirst/0`.
- `"myfirst/control"` gives `/dev/myfirst/control`, with no unit number at all.

Grouping related nodes into a subdirectory is a common pattern for drivers that expose more than one surface. Think of `/dev/led/*` from `/usr/src/sys/dev/led/led.c`, or `/dev/pf`, `/dev/pflog*`, and friends from the packet-filter subsystem. The subdirectory makes the relationship obvious at a glance, keeps the top level of `/dev` tidy, and lets an operator grant or deny access to the whole set with a single `devfs.conf` line.

You will adopt this pattern for `myfirst` in this chapter. The main data path moves from `/dev/myfirst0` to `/dev/myfirst/0`. Then you will add an alias so the old path keeps working for any lab scripts that remember the previous layout.

### Names in the Real FreeBSD Tree

Browsing the `/dev` of a running FreeBSD system is educational in its own right, because the naming conventions you see there were shaped by the same pressures your driver will face. A short tour, grouped by theme:

- **Direct device names.** `/dev/null`, `/dev/zero`, `/dev/random`, `/dev/urandom`. One cdev per node, top-level, short stable names. Good for singletons with no hierarchy.
- **Unit-numbered names.** `/dev/bpf0`, `/dev/bpf1`, `/dev/ttyu0`, `/dev/md0`. One cdev per instance, numbered from zero. The format string looks like `"bpf%d"` and the driver manages the unit numbers.
- **Subdirectories per driver.** `/dev/led/*`, `/dev/pts/*`, `/dev/ipmi*` in some configurations. Used when a single driver exposes many related nodes. Makes operator policy simple: one `devfs.conf` or `devfs.rules` entry can cover the whole set.
- **Split data and control nodes.** `/dev/bpf` (the cloning entry point) plus per-open clones, `/dev/fido/*` for FIDO devices, and so on. Used when a driver needs different semantics for discovery versus data.
- **Aliased names for convenience.** `/dev/stdin`, `/dev/stdout`, `/dev/stderr` are symlinks that devfs provides for the current process's file descriptors. `/dev/random` and `/dev/urandom` were once aliased; in modern FreeBSD they are separate nodes served by the same random driver but the history is still visible.

You do not need to memorise these patterns. You do need to recognise them, because when you read existing drivers they will all make more sense once the naming convention is named.

### Multiple Nodes Per Device

Some drivers expose one node and that is enough. Other drivers expose several, each with different semantics. A common split is:

- A **data node** that carries the bulk payload (reads, writes, mmaps) and is meant for high-throughput use.
- A **control node** that carries management traffic (configuration, status, reset) and is typically group-readable for monitoring tools.

When a driver does this, it calls `make_dev_s(9)` twice in `attach()` and keeps both cdev pointers in the softc. In Chapter 8 you will stop at one data node plus one alias, but the pattern is worth knowing now so you recognise it when you see it.

Lab 8.5 builds out a minimal two-node variant of `myfirst` with a data node at `/dev/myfirst/0` and a control node at `/dev/myfirst/0.ctl`. Each node has its own `cdevsw` and its own permission mode. The lab is there to show how the pattern looks in code; most of your drivers in later chapters will use it.

### The make_dev Family in Depth

You have used `make_dev_s(9)` for every node you have created so far. FreeBSD actually provides a small family of `make_dev*` functions, each with slightly different ergonomics. Reading existing drivers will expose you to all of them, and knowing when to use which saves grief later.

The full declarations live in `/usr/src/sys/sys/conf.h`. In order of increasing modernity:

```c
struct cdev *make_dev(struct cdevsw *_devsw, int _unit, uid_t _uid, gid_t _gid,
                      int _perms, const char *_fmt, ...);

struct cdev *make_dev_cred(struct cdevsw *_devsw, int _unit,
                           struct ucred *_cr, uid_t _uid, gid_t _gid, int _perms,
                           const char *_fmt, ...);

struct cdev *make_dev_credf(int _flags, struct cdevsw *_devsw, int _unit,
                            struct ucred *_cr, uid_t _uid, gid_t _gid, int _mode,
                            const char *_fmt, ...);

int make_dev_p(int _flags, struct cdev **_cdev, struct cdevsw *_devsw,
               struct ucred *_cr, uid_t _uid, gid_t _gid, int _mode,
               const char *_fmt, ...);

int make_dev_s(struct make_dev_args *_args, struct cdev **_cdev,
               const char *_fmt, ...);
```

Take them one at a time.

**`make_dev`** is the original positional-argument form. It returns the new cdev pointer directly, or panics on any error. Panicking on error is a strong hint that it is intended for code paths that cannot recover, such as the very early initialization of truly-eternal devices. Avoid it in new drivers. It is still in the tree only because older drivers use it, and because some of those drivers are places where an early panic is genuinely acceptable.

**`make_dev_cred`** adds a credential (`struct ucred *`) argument. The credential is used by devfs when applying rules; it tells the system "this cdev was created by this credential" for rule-matching purposes. Most drivers pass `NULL` for the credential and get the default behavior. You will see this form in drivers that clone devices on demand in response to user requests; it is not common elsewhere.

**`make_dev_credf`** extends `make_dev_cred` with a flags word. This is the first member of the family that lets you say "do not panic if this fails; return `NULL` so I can handle it".

**`make_dev_p`** is a functional equivalent to `make_dev_credf` with a cleaner return-value convention: it returns an `errno` value (zero on success) and writes the new cdev pointer through an output parameter. This is the form most widely used in modern code bases that were written before `make_dev_s` existed.

**`make_dev_s`** is the modern recommended form. It accepts a pre-populated `struct make_dev_args` (initialised with `make_dev_args_init_impl` and described below) and writes the cdev pointer through an output parameter. It returns an `errno` value, zero on success. The reason the book uses it is simple: it is the easiest form to read, the easiest form to extend (adding a new field to the argument struct is ABI-friendly), and the easiest form to error-check.

The argument structure, also from `/usr/src/sys/sys/conf.h`:

```c
struct make_dev_args {
        size_t         mda_size;
        int            mda_flags;
        struct cdevsw *mda_devsw;
        struct ucred  *mda_cr;
        uid_t          mda_uid;
        gid_t          mda_gid;
        int            mda_mode;
        int            mda_unit;
        void          *mda_si_drv1;
        void          *mda_si_drv2;
};
```

`mda_size` is set automatically by `make_dev_args_init(a)`; you never touch it. `mda_flags` carries the `MAKEDEV_*` flags described below. `mda_devsw`, `mda_cr`, `mda_uid`, `mda_gid`, `mda_mode`, and `mda_unit` correspond to the positional arguments of the older forms. `mda_si_drv1` and `mda_si_drv2` let you pre-populate the driver pointer slots on the resulting cdev, which is how you avoid the window where `si_drv1` could briefly be `NULL` after `make_dev_s` returns but before you assign it. Always populate `mda_si_drv1` before the call.

### Which Form Should You Use?

For new drivers, **use `make_dev_s`**. Every example in this book uses it, and every driver you write for yourself should do the same unless a very specific reason forces otherwise.

For reading existing code, recognise all of them. If you find a driver that calls `make_dev(...)` and ignores its return value, you are looking at either a driver that predates the modern APIs or a driver whose authors decided a panic on failure is acceptable. Both are defensible in context; neither is the right default for new code.

### The MAKEDEV_* Flags

The flags that can be OR-ed into `mda_flags` (or passed as the first argument to `make_dev_p` and `make_dev_credf`) are defined in `/usr/src/sys/sys/conf.h`. Each one carries a specific meaning:

- **`MAKEDEV_REF`**: increments the reference count on the resulting cdev by one, in addition to the usual reference. Used when the caller plans to hold the cdev pointer long-term across events that would ordinarily drop the reference. Rare in beginner-level drivers.
- **`MAKEDEV_NOWAIT`**: tells the allocator not to wait if memory is tight. On an out-of-memory condition the function returns `ENOMEM` (for `make_dev_s`) or `NULL` (for older forms) instead of blocking. Use this only if your caller cannot afford to sleep.
- **`MAKEDEV_WAITOK`**: the inverse. Tells the allocator it is safe to sleep for memory. This is the default for `make_dev` and `make_dev_s`, so you rarely spell it out.
- **`MAKEDEV_ETERNAL`**: marks the cdev as never-to-be-destroyed. devfs will refuse to honor `destroy_dev(9)` on it during normal operation. Used by the eternal in-kernel devices such as `null`, `zero`, and `full`. Do not set this in a driver you plan to unload.
- **`MAKEDEV_CHECKNAME`**: asks the function to validate the node name against devfs's rules before creating it. On failure it returns an error rather than creating a badly-named cdev. Useful in code paths that synthesise names from user input.
- **`MAKEDEV_WHTOUT`**: creates a "whiteout" entry, used in conjunction with stacked filesystems to mask an underlying entry. Not something you will encounter in driver work.
- **`MAKEDEV_ETERNAL_KLD`**: a macro that expands to `MAKEDEV_ETERNAL` when the code is built outside a loadable module and to zero when it is built as a KLD. This lets shared source for a device (like `null`) set the flag when statically compiled and clear it when loaded as a module, so that the module remains unloadable.

For a typical beginner-level driver, the flag field is zero, which is what the `myfirst` examples in the companion tree use. `MAKEDEV_CHECKNAME` is worth reaching for when the node name is built from user input or from a string whose provenance you do not fully control; for a driver that passes a constant format string such as `"myfirst/%d"`, the flag adds nothing useful.

### The cdevsw d_flags

Separate from the `MAKEDEV_*` flags, the `cdevsw` itself carries a `d_flags` field that shapes how devfs and other kernel machinery treat the cdev. These flags were listed in the cdevsw tour a few sections back; this section is the place to understand when to set them.

**`D_TRACKCLOSE`** is the flag you are most likely to want in Chapter 8. By default, devfs calls your `d_close` only when the last file descriptor referring to the cdev is released. If a process has called `dup(2)` or `fork(2)` and two descriptors share the open, `d_close` fires once, at the very end. That is often what you want. It is not what you want if you need a reliable per-descriptor close hook. Setting `D_TRACKCLOSE` makes devfs call `d_close` for every `close(2)` on every descriptor. For a driver that uses `devfs_set_cdevpriv(9)` for per-open state, the destructor is usually the better hook; `D_TRACKCLOSE` remains useful when the semantics of your device genuinely require each close to be observable.

**`D_MEM`** tags the cdev as a memory-style device; `/dev/mem` itself sets this. It changes how certain kernel paths treat I/O to the node.

**`D_DISK`**, **`D_TAPE`**, **`D_TTY`** are hints for the category of device. Modern drivers mostly do not set them, because GEOM owns disks, the TTY subsystem owns TTYs, and tape devices route through their own layer. You will see them on legacy drivers.

**`D_MMAP_ANON`** alters how mapping the device yields pages. The `zero` device sets it; mapping `/dev/zero` yields anonymous, zero-filled pages. Useful to recognise; you will not need to set it until you write a driver that wants the same semantics.

**`D_NEEDGIANT`** requests that all `cdevsw` handlers for this cdev be dispatched under the Giant lock. It exists as a safety blanket for drivers that have not been audited for SMP. A new driver should not set this flag. If you see it in code written after 2010 or so, treat it with suspicion.

**`D_NEEDMINOR`** tells devfs that the driver uses `clone_create(9)` to allocate minor numbers on demand. You will not encounter this until you write a cloning driver, which is out of scope for this chapter.

The flags you will set in `myfirst` are, in most versions, none. Once Chapter 8 adds per-open state, the driver still does not need `D_TRACKCLOSE` because the cdevpriv destructor covers the per-descriptor cleanup need.

### Name Length and Name Characters

`make_dev_s` accepts a printf-style format and produces a name that devfs stores in the cdev's `si_name` field. The size of that field is `SPECNAMELEN + 1`, and `SPECNAMELEN` is currently 255. A name longer than that is an error.

In addition to length, a name must be acceptable as a filesystem path under devfs. That means it must not contain null bytes, it must not use `.` or `..` as components, and it should not use characters that shells or scripts interpret specially. The safest set is lowercase ASCII letters, digits, and the three separators `/`, `-`, and `.`. Other characters will sometimes work and sometimes not; if you are ever tempted to use spaces, colons, or non-ASCII characters in a device name, stop and pick a simpler name instead.

### Unit Numbers: Where They Come From

Unit numbers are small integers that distinguish instances of the same driver. They appear in the device name (`myfirst0`, `myfirst1`), in `sysctl` branches (`dev.myfirst.0`, `dev.myfirst.1`), and in the cdev's `si_drv0` field.

Two ways are common for assigning them:

**Newbus assignment.** When your driver attaches to a bus and Newbus instantiates a device, the bus assigns a unit number. You retrieve it with `device_get_unit(9)` and use it as `sc->unit`, exactly as Chapter 7 does. Newbus guarantees the number is unique within the driver's namespace.

**Explicit allocation with `unrhdr`.** For drivers that create nodes outside the Newbus flow, the `unrhdr(9)` allocator assigns unit numbers from a pool. `/usr/src/sys/dev/led/led.c` uses this: `sc->unit = alloc_unr(led_unit);`. The LED framework does not attach through Newbus for each LED, so it cannot ask Newbus for a unit number; it maintains its own unit pool.

For a beginner driver built on Newbus, the first approach is the one to use. The second becomes relevant once you write a pseudo-device that can be instantiated many times on demand, which is a later-chapter topic.

### Naming Conventions in the Tree

Since you are likely to read real FreeBSD drivers as part of learning, it helps to recognise the shapes their names take. A short tour:

- **`bpf%d`**: one node per BPF instance. Seen in `/usr/src/sys/net/bpf.c`.
- **`md%d`**: memory disks. `/usr/src/sys/dev/md/md.c`.
- **`led/%s`**: subdirectory per driver, one node per LED. `/usr/src/sys/dev/led/led.c` uses the name argument as a free-form string, chosen by the caller, e.g. `led/ehci0`.
- **`ttyu%d`**, **`cuaU%d`**: hardware serial ports, paired "in" and "out" nodes.
- **`ptyp%d`**, **`ttyp%d`**: pseudo-terminal pairs.
- **`pts/%d`**: modern PTY allocation in a subdirectory.
- **`fuse`**: singleton entry point for the FUSE subsystem.
- **`mem`**, **`kmem`**: singletons for memory inspection.
- **`pci`, `pciconf`**: PCI bus inspection interfaces.
- **`io`**: I/O-port access, singleton.
- **`audit`**: audit subsystem control device.

Notice that in most of these the name encodes the driver's identity. That is deliberate. When an operator later needs to write a `devfs.conf` rule or a firewall rule or a backup script, they match on paths, and predictable paths make their life easier.

### Handling Multiple Units

Your Chapter 7 driver registered exactly one Newbus child in its `device_identify` callback, so there is only one instance and the only unit number is `0`. Some drivers want more than one instance, either at boot or on demand.

For a driver instantiated at boot with a fixed count, the pattern is to add more children in `device_identify`:

```c
static void
myfirst_identify(driver_t *driver, device_t parent)
{
        int i;

        for (i = 0; i < MYFIRST_INSTANCES; i++) {
                if (device_find_child(parent, driver->name, i) != NULL)
                        continue;
                if (BUS_ADD_CHILD(parent, 0, driver->name, i) == NULL)
                        device_printf(parent,
                            "myfirst%d: BUS_ADD_CHILD failed\n", i);
        }
}
```

Newbus calls `attach` for each child, and each call gets its own softc and its own unit number. Your `make_dev_s` format string `"myfirst/%d"` with `sc->unit` then produces `/dev/myfirst/0`, `/dev/myfirst/1`, and so on.

For a driver instantiated on demand, the architecture is very different. You typically expose a single "control" cdev, and when a user performs an operation on it the driver allocates a new instance and a new cdev. The memory disk driver in `/usr/src/sys/dev/md/md.c` is a clear example: `/dev/mdctl` accepts a `MDIOCATTACH` ioctl, and each successful attach produces a new `/dev/mdN` cdev through the GEOM layer. The pseudo-terminal subsystem takes a similar approach: a user opening `/dev/ptmx` is handed a freshly allocated `/dev/pts/N` on the other side. Chapter 8 does not walk you through those machineries; it is enough to know that when you see a driver create cdevs from inside an event handler rather than from `attach`, dynamic instantiation is the pattern you are looking at.

### A Small Detour: devtoname and Friends

Three small helpers come up often in driver code and in the book from here on. They are worth collecting:

- **`devtoname(cdev)`**: returns a pointer to the node's name. Read-only. Used for logging: `device_printf(dev, "created /dev/%s\n", devtoname(sc->cdev))`.
- **`dev2unit(cdev)`**: returns the `si_drv0` field, which is conventionally the unit number. Defined as a macro in `conf.h`.
- **`device_get_nameunit(dev)`**: used on a `device_t`, returns the Newbus-scoped name such as `"myfirst0"`. Useful for mutex names.

All three are safe to use in contexts where the cdev or device is known to be alive, which for a driver handler is always.



## Aliases: One cdev, More Than One Name

Sometimes a device needs to be reachable under more than one name. Maybe you renamed a node and want the old name to keep working during a deprecation window. Maybe you want a stable short name that always points at unit `0` without the user having to know which unit is current. Maybe the rest of the system already has a strong convention and you want to play nicely with it.

FreeBSD offers `make_dev_alias(9)` for this. An alias is itself a `struct cdev`, but one that carries the `SI_ALIAS` flag and shares the same underlying dispatch machinery as the primary node. A user program opening the alias lands in the same `cdevsw` handlers as a user program opening the primary name.

Signatures, from `/usr/src/sys/sys/conf.h`:

```c
struct cdev *make_dev_alias(struct cdev *_pdev, const char *_fmt, ...);
int          make_dev_alias_p(int _flags, struct cdev **_cdev,
                              struct cdev *_pdev, const char *_fmt, ...);
```

You pass in the primary cdev, a format string, and optional arguments. You get back a new cdev that represents the alias. When you are done, you destroy the alias with `destroy_dev(9)`, the same way you destroy any other cdev.

Here is the shape of the code you will add to `myfirst_attach()`:

```c
sc->cdev_alias = make_dev_alias(sc->cdev, "myfirst");
if (sc->cdev_alias == NULL) {
        device_printf(dev, "failed to create /dev/myfirst alias\n");
        /* fall through; the primary node is still usable */
}
```

Two observations about that snippet. First, failing to create an alias is not fatal. The primary path still works, so we log and continue. Second, you only need to keep a pointer to the alias cdev if you plan to destroy it on detach. In most drivers you do, so stash it in the softc right next to `cdev`.

### Aliases vs `link` in devfs.conf

Readers familiar with UNIX symbolic links sometimes ask why FreeBSD offers two different ways to give a device a second name. The distinction is real and worth stating clearly.

A `make_dev_alias(9)` alias is a **second cdev that shares its dispatch machinery with the primary**. When the user opens it, devfs walks directly to your `cdevsw` handlers. There is no symlink in the filesystem. `ls -l` on the alias shows another character-special node with its own mode and ownership. The kernel knows the alias is bound to the primary cdev (the `SI_ALIAS` flag and `si_parent` pointer record that relationship) and cleans it up automatically if the primary goes away, provided your driver remembers to call `destroy_dev(9)` on it.

A `link` directive in `/etc/devfs.conf` creates a **symbolic link inside devfs**. `ls -l` shows an `l` in the type field and an arrow pointing at the target. Open it and the kernel first resolves the symlink, then opens the target. The target and the link have independent permission and ownership; the symlink itself carries no access policy beyond its existence.

Which to choose?

- Use `make_dev_alias` when the driver itself has a reason to expose the extra name, for instance a short well-known form or a legacy path that must look identical to the new one at the permission level.
- Use `link` in `devfs.conf` when the operator wants a convenience shortcut and the driver has no opinion. Nothing about that sort of link belongs in kernel code.

Both approaches work. The wrong choice is not dangerous; it is usually just clumsy. Keep driver code lean and let operator policy stay where policy lives.

### Comparison Table: Three Ways to Give a Node Two Names

A short comparison pulls the distinctions into one place:

| Property                          | `make_dev_alias` | `devfs.conf link` | Symbolic link via `ln -s` |
|-----------------------------------|:----------------:|:-----------------:|:-------------------------:|
| Lives in kernel code              | yes              | no                | no                        |
| Lives in devfs                    | yes              | yes               | no (lives in underlying FS)|
| `ls -l` shows as `c`              | yes              | no (shows as `l`) | no (shows as `l`)         |
| Carries its own mode and owner    | yes              | inherits target   | inherits target           |
| Auto-cleaned on driver unload     | yes              | yes (next `service devfs restart`) | no |
| Survives a reboot                 | only while driver is loaded | yes, if in `devfs.conf` | yes, if under `/etc` or similar |
| Appropriate for driver-owned name | yes              | no                | no                        |
| Appropriate for operator shortcut | no               | yes               | sometimes                 |

The pattern is: drivers own their primary names and any aliases that carry policy; operators own convenience links that do not carry policy. Crossing that line is where future maintenance pain comes from.

### The `make_dev_alias_p` Variant

`make_dev_alias` has a sibling that accepts a flags word and returns an `errno`, for the same reasons the main `make_dev` family does. Its declaration in `/usr/src/sys/sys/conf.h`:

```c
int make_dev_alias_p(int _flags, struct cdev **_cdev, struct cdev *_pdev,
                     const char *_fmt, ...);
```

The valid flags are `MAKEDEV_WAITOK`, `MAKEDEV_NOWAIT`, and `MAKEDEV_CHECKNAME`. Behavior is analogous to `make_dev_p`: zero on success, the new cdev written through the output pointer, a non-zero `errno` value on failure.

If your alias creation is in a path that cannot sleep, use `make_dev_alias_p(MAKEDEV_NOWAIT, ...)` and be prepared for `ENOMEM`. In the ordinary case where your alias is created during `attach` under regular conditions, `make_dev_alias(9)` is fine; it uses `MAKEDEV_WAITOK` internally.

### The `make_dev_physpath_alias` Variant

There is a third alias function, `make_dev_physpath_alias`, used by drivers that want to publish physical-path aliases in addition to their logical names. It exists to support the hardware-topology paths under `/dev/something/by-path/...` that certain storage drivers expose. Most beginner drivers never need it.

### Reading Uses of `make_dev_alias` in the Tree

A useful exercise: `grep` for `make_dev_alias` across `/usr/src/sys` and look at the contexts where it is used. You will find it in storage drivers that want to publish a stable name alongside a dynamically-numbered one, in certain pseudo-devices that want a legacy compatibility name, and in a small number of specialised drivers that model a hardware topology.

Most drivers do not use it, and that is fine. When a driver does, the reason is almost always one of three:

1. **Legacy path compatibility.** A driver that was renamed but must keep the old name working.
2. **A well-known shortcut.** A short name that always resolves to instance zero or to the current default, so shell scripts can write one path instead of negotiating the unit number.
3. **Topology exposure.** A name that reflects where the hardware sits, in addition to what the hardware is.

Your `myfirst` driver is using case 1: `/dev/myfirst` as a shortcut for `/dev/myfirst/0` so the Chapter 7 prose still resolves. That is the shape of a typical beginner use.

### Alias Lifetimes and Destruction Order

A cdev that is registered as an alias has the `SI_ALIAS` flag set and is linked into the primary cdev's `si_children` list with the `si_parent` back-pointer. This means the kernel knows the relationship and will do the right thing even if you tear the cdev down in a slightly wrong order. It does not mean you can ignore the order; it means destruction is more forgiving than teardown of general kernel objects.

In practice, the rule you should follow in your `detach` path is: **destroy the alias first, then the primary**. The example drivers in the companion tree do this, and the reason is simple readability. Any other order makes your code harder to reason about, and reviewers will flag it.

If a driver omits the `destroy_dev` call on the alias entirely, the primary's destruction will unwind the alias automatically when the primary goes away; that is what `destroy_devl` does when it walks `si_children`. But leaving that work to the destructor is wasteful because the primary is holding a reference that keeps it alive longer than needed, and because the operator sees the alias disappear "later" rather than cleanly at unload time. Just destroy both.

### When Aliases Start to Smell

A few patterns with aliases are mild code smells worth naming:

- **Chains of aliases.** Aliases of aliases are legal but almost always mean the driver is trying to paper over a naming decision that should have been revisited. If you find yourself wanting to alias an alias, stop and rename the primary.
- **Too many aliases.** One or two is routine. Five or more suggests the driver is not sure what it wants to be called. Revisit the naming.
- **Aliases with wildly different modes.** Two cdevs that point at the same handler set but expose radically different permission modes are indistinguishable from a trap. Make the permissions consistent, or use two separate primaries with two separate `cdevsw` values that enforce different policies in code.

None of these are errors. They are signals that the design is drifting. Notice them early and the driver stays readable; ignore them and the driver becomes a thing reviewers dread.



## Per-Open State with devfs_set_cdevpriv

Now we come to the piece of the chapter that prepares the ground for Chapter 9. Your Chapter 7 driver enforced **exclusive open** by setting a flag in the softc. That works, but it is the coarsest possible policy. Many real devices allow several openers and want to keep a small amount of bookkeeping **per file descriptor**, not per device. Think of a log stream, a status feed, or any node where different consumers want their own read positions.

FreeBSD offers three related routines for this, declared in `/usr/src/sys/sys/conf.h` and implemented in `/usr/src/sys/fs/devfs/devfs_vnops.c`:

```c
int  devfs_set_cdevpriv(void *priv, d_priv_dtor_t *dtr);
int  devfs_get_cdevpriv(void **datap);
void devfs_clear_cdevpriv(void);
```

The model is simple and pleasant to use:

1. Inside your `d_open` handler, allocate a small per-open structure and call `devfs_set_cdevpriv(priv, dtor)`. The kernel attaches `priv` to the current file descriptor and remembers `dtor` as the function to call when that descriptor eventually closes.
2. In `d_read`, `d_write`, or any other handler, call `devfs_get_cdevpriv(&priv)` to retrieve the pointer.
3. When the process calls `close(2)`, or exits, or otherwise drops its last reference to the descriptor, devfs calls your destructor with `priv`. You free whatever you allocated.

You do not need to worry about the order of cleanup with respect to your own `d_close` handler. Devfs handles it. The important invariant is that your destructor will be called exactly once per successful `devfs_set_cdevpriv`.

A real example from `/usr/src/sys/net/bpf.c` looks like this:

```c
d = malloc(sizeof(*d), M_BPF, M_WAITOK | M_ZERO);
error = devfs_set_cdevpriv(d, bpf_dtor);
if (error != 0) {
        free(d, M_BPF);
        return (error);
}
```

That is essentially the whole pattern. BPF allocates a per-open descriptor, registers it, and if the registration fails, frees the allocation and returns the error. The destructor `bpf_dtor` cleans up when the descriptor dies. You will do the same thing for `myfirst`, with a much smaller per-open structure.

### A Minimal Per-Open Counter for myfirst

You will add one small structure and one destructor. Nothing else in the driver changes shape.

```c
struct myfirst_fh {
        struct myfirst_softc *sc;    /* back-pointer to the owning softc */
        uint64_t              reads; /* bytes this descriptor has read */
        uint64_t              writes;/* bytes this descriptor has written */
};

static void
myfirst_fh_dtor(void *data)
{
        struct myfirst_fh *fh = data;
        struct myfirst_softc *sc = fh->sc;

        mtx_lock(&sc->mtx);
        sc->active_fhs--;
        mtx_unlock(&sc->mtx);

        device_printf(sc->dev, "per-open dtor fh=%p reads=%lu writes=%lu\n",
            fh, (unsigned long)fh->reads, (unsigned long)fh->writes);

        free(fh, M_DEVBUF);
}
```

The destructor does three things worth noticing. It decrements `active_fhs` under the same mutex that guards the other softc counters, so the count stays consistent with what `d_open` saw when it opened the descriptor. It logs a line that matches the shape of the `open via ...` message, so every open in `dmesg` has a visibly paired destructor. And it frees the allocation last, after everything that might need to read from `fh` has already run.

In your `d_open`, allocate one of these and register it:

```c
static int
myfirst_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
        struct myfirst_softc *sc;
        struct myfirst_fh *fh;
        int error;

        sc = dev->si_drv1;
        if (sc == NULL || !sc->is_attached)
                return (ENXIO);

        fh = malloc(sizeof(*fh), M_DEVBUF, M_WAITOK | M_ZERO);
        fh->sc = sc;

        error = devfs_set_cdevpriv(fh, myfirst_fh_dtor);
        if (error != 0) {
                free(fh, M_DEVBUF);
                return (error);
        }

        mtx_lock(&sc->mtx);
        sc->open_count++;
        sc->active_fhs++;
        mtx_unlock(&sc->mtx);

        device_printf(sc->dev, "open via %s fh=%p (active=%d)\n",
            devtoname(dev), fh, sc->active_fhs);
        return (0);
}
```

Notice two things. First, the exclusive-open check from Chapter 7 is gone. With per-open state in place, there is no reason to refuse a second opener. If you do want exclusivity later, you can still add it back in; it is a separate decision. Second, the destructor will take care of the free. Your `d_close` does not need to touch `fh` at all.

In a handler that runs later, such as `d_read`, you retrieve the per-open structure:

```c
static int
myfirst_read(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct myfirst_fh *fh;
        int error;

        error = devfs_get_cdevpriv((void **)&fh);
        if (error != 0)
                return (error);

        /* Real read logic arrives in Chapter 9. For now, report EOF
         * and leave the counter untouched so userland tests can observe
         * that the descriptor owns its own state.
         */
        (void)fh;
        return (0);
}
```

The `(void)fh` silences the "unused variable" warning until Chapter 9 gives it work to do. That is fine. What matters for now is that your driver has a clean, working, cleanly destroyed per-file structure. From userland you can confirm the wiring by opening the device from two processes and watching the device-printf messages come through with two different `fh=` pointers.

### What the Destructor Guarantees

Because the destructor does most of the work, it is worth being precise about when it runs and what state the world is in at that moment. Reading `devfs_destroy_cdevpriv` in `/usr/src/sys/fs/devfs/devfs_vnops.c` confirms the details.

- The destructor runs **exactly once per successful `devfs_set_cdevpriv`** call. If the function returned `EBUSY` because the descriptor already had private data, the destructor for *your* data is never invoked; you must free the allocation yourself, as the example code does.
- The destructor runs **when the file descriptor is released**, not when your `d_close` is called. For an ordinary `close(2)`, the two moments are close together. For a process that exits while holding the descriptor, the descriptor is released as part of exit teardown; the destructor still runs. For a descriptor shared through `fork(2)` or passed through a UNIX-domain socket, the destructor runs only when the last reference drops.
- The destructor runs with no kernel lock held on your behalf. If your destructor touches softc state, take whatever lock the softc uses, just as the stage 2 example does when decrementing `active_fhs`.
- The destructor must not block for long. It is not a sleep-forever context, but it is also not an interrupt handler. Treat it like an ordinary kernel function and keep it short.

### When `devfs_set_cdevpriv` Returns EBUSY

`devfs_set_cdevpriv` can fail in exactly one interesting way: the descriptor already has private data associated with it. That happens when something, typically your own code in a previous call, has already set a cdevpriv and you are trying to set another. The clean fix is to do the set once, early, and then read it back with `devfs_get_cdevpriv` wherever you need it.

Two cautions follow from this. The first is: do not call `devfs_set_cdevpriv` twice from the same open. The second is: when the call fails, free whatever you allocated before trying to set it. The example `myfirst_open` in this chapter follows both rules. When you port the pattern to your own driver, keep them in view.

### When Not to Use devfs_set_cdevpriv

Per-open state is not the right home for everything. Keep device-wide state in the softc, reachable through `si_drv1`. Keep per-open state in the cdevpriv structure, reachable through `devfs_get_cdevpriv`. Mixing the two is the quickest way to write a driver that works in single-opener tests and falls apart when two processes show up at once.

`devfs_clear_cdevpriv(9)` exists, and you may see it in third-party code, but for most drivers the automatic cleanup through the destructor is sufficient. Reach for `devfs_clear_cdevpriv` only when you have a concrete reason, for instance a driver that can cleanly detach per-open state early in response to an `ioctl(2)`. If you are not sure you need it, you do not.

### Inside devfs_set_cdevpriv: How the Machinery Works

The two functions you call look almost trivial from the outside. The machinery they drive is worth looking at once, because knowing the shape of it makes every edge case easier to reason about.

From `/usr/src/sys/fs/devfs/devfs_vnops.c`:

```c
int
devfs_set_cdevpriv(void *priv, d_priv_dtor_t *priv_dtr)
{
        struct file *fp;
        struct cdev_priv *cdp;
        struct cdev_privdata *p;
        int error;

        fp = curthread->td_fpop;
        if (fp == NULL)
                return (ENOENT);
        cdp = cdev2priv((struct cdev *)fp->f_data);
        p = malloc(sizeof(struct cdev_privdata), M_CDEVPDATA, M_WAITOK);
        p->cdpd_data = priv;
        p->cdpd_dtr = priv_dtr;
        p->cdpd_fp = fp;
        mtx_lock(&cdevpriv_mtx);
        if (fp->f_cdevpriv == NULL) {
                LIST_INSERT_HEAD(&cdp->cdp_fdpriv, p, cdpd_list);
                fp->f_cdevpriv = p;
                mtx_unlock(&cdevpriv_mtx);
                error = 0;
        } else {
                mtx_unlock(&cdevpriv_mtx);
                free(p, M_CDEVPDATA);
                error = EBUSY;
        }
        return (error);
}
```

A short walk through the important bits:

- `curthread->td_fpop` is the file pointer for the current dispatch. devfs sets this up before it calls into your `d_open` and unsets it afterward. If you called `devfs_set_cdevpriv` from a context where no dispatch is active, `fp` would be `NULL` and the function would return `ENOENT`. In practice this only happens if you try to call it from the wrong context, for instance from a timer callback that is not bound to a file.
- A small record, `struct cdev_privdata`, is allocated from a dedicated malloc bucket `M_CDEVPDATA`. It carries three fields: your pointer, your destructor, and a back-pointer to the `struct file`.
- Two threads entering this function at the same time for the same descriptor would be a disaster, so a single mutex `cdevpriv_mtx` guards the critical section. The check for `fp->f_cdevpriv == NULL` is what prevents double-registration: if a record is already attached, the new record is freed and `EBUSY` comes back.
- On success the record is inserted onto two lists: the descriptor's own pointer `fp->f_cdevpriv`, and the cdev's list of all its descriptor-private records `cdp->cdp_fdpriv`. The first makes `devfs_get_cdevpriv` a one-pointer lookup. The second makes it possible for devfs to iterate every live record when the cdev is destroyed.

The destructor path is equally small:

```c
void
devfs_destroy_cdevpriv(struct cdev_privdata *p)
{

        mtx_assert(&cdevpriv_mtx, MA_OWNED);
        KASSERT(p->cdpd_fp->f_cdevpriv == p,
            ("devfs_destoy_cdevpriv %p != %p",
             p->cdpd_fp->f_cdevpriv, p));
        p->cdpd_fp->f_cdevpriv = NULL;
        LIST_REMOVE(p, cdpd_list);
        mtx_unlock(&cdevpriv_mtx);
        (p->cdpd_dtr)(p->cdpd_data);
        free(p, M_CDEVPDATA);
}
```

Two things to notice. First, the destructor is called **with the mutex dropped**, so your destructor can take locks of its own without a risk of a deadlock against `cdevpriv_mtx`. Second, the record itself is freed immediately after your destructor returns, so a stale pointer into it would be a use-after-free. If your destructor stashes the pointer somewhere else, stash a copy of the data, not the record.

### Interaction with fork, dup, and SCM_RIGHTS

File descriptors in UNIX have three common ways of multiplying: `dup(2)`, `fork(2)`, and passing through a UNIX-domain socket with `SCM_RIGHTS`. Each produces additional references to the same `struct file`. devfs's cdevpriv machinery behaves consistently across all three.

After `dup(2)` or `fork(2)`, the new file descriptor refers to the **same** `struct file` as the original. The cdevpriv record is keyed on the `struct file`, not the descriptor number, so both descriptors share the record. Your destructor fires exactly once, when the last descriptor pointing at that file is released. That last-release can be an explicit `close(2)`, an implicit `exit(3)` that closes everything, or even a crash that terminates the process.

Passing the descriptor through `SCM_RIGHTS` is the same story from the cdevpriv's point of view. The receiving process gets a new descriptor that points at the same `struct file`. The record stays attached; the destructor still fires only when the last reference drops, which may now be in the process on the other end of the socket.

This is usually exactly what you want, because it matches the user's mental model. One per-open state per conceptual open. If you ever need a different model, for instance a model where every `dup(2)`'d descriptor should have its own state, the solution is to set `D_TRACKCLOSE` on your `cdevsw` and allocate per-descriptor state inside `d_open` itself without using `devfs_set_cdevpriv`. That is unusual; ordinary drivers do not need it.

### A Tour of Real Uses in the Tree

To cement the pattern, a short tour of three drivers that use cdevpriv in recognisable ways. You do not need to understand what each driver does as a whole; focus only on the device-file shape.

**`/usr/src/sys/net/bpf.c`** is the canonical example. Its `bpfopen` allocates a per-open descriptor, calls `devfs_set_cdevpriv(d, bpf_dtor)`, and sets up a small pile of counters and state. The destructor `bpf_dtor` tears down all of it: it detaches the descriptor from its BPF interface, frees counters, drains a select list, and drops a reference. The pattern is exactly the one this chapter has described, plus a lot of BPF-specific machinery that Part 6 will revisit.

**`/usr/src/sys/fs/fuse/fuse_device.c`** takes the same pattern and layers FUSE-specific state on top. The open allocates a `struct fuse_data`, registers it with `devfs_set_cdevpriv`, and every subsequent handler retrieves it with `devfs_get_cdevpriv`. The destructor tears down the FUSE session.

**`/usr/src/sys/opencrypto/cryptodev.c`** uses cdevpriv for per-open crypto session state. Each open gets its own bookkeeping, and the destructor cleans it up.

These three drivers have almost nothing in common at the subsystem level: one is about packet capture, one about userland filesystems, one about hardware crypto offload. What they share is the device-file shape. The same three steps, in the same order, for the same reasons.

### Patterns for What to Put in the Per-Open Structure

Now that you know the mechanics, the design question is what fields your per-open structure should hold. A few patterns recur across real drivers.

**Counters.** Bytes read, bytes written, calls made, errors reported. Each descriptor owns its own counter. `myfirst` in stage 2 already does this with `reads` and `writes`.

**Read positions.** If your driver exposes a seekable stream of bytes, the current offset belongs in the per-open structure, not in the softc. Two readers at different offsets are the reason.

**Subscription handles.** If the descriptor is reading events and a `poll` or `kqueue` needs to know whether there are more events pending for this specific descriptor, the subscription record belongs here. Chapter 10 uses this pattern.

**Filter state.** Drivers like BPF let each descriptor install a filter program. That program's compiled form is per-descriptor. Again, belongs in the per-open structure.

**Reservations or tickets.** If the driver hands out scarce resources (a hardware slot, a DMA channel, a shared buffer range) and ties them to an open, the record goes into per-open state. When the descriptor closes, the destructor frees the reservation automatically.

**Credentials snapshots.** Some drivers want to remember who opened the descriptor at the time of open, separately from whoever is currently doing a read or write on it. Capturing a snapshot of `td->td_ucred` at open time is a common pattern. Credentials are reference-counted (`crhold`/`crfree`), and the destructor is the right place to drop the reference.

Not every driver needs all of these. The list is a menu, not a checklist. When you design a driver, walk through it and ask "what information belongs to this specific open of the node?" The answers go into the per-open structure.

### A Warning Against Cross-Referencing From the softc to Per-Open Records

A temptation that crops up with per-open state is for the softc to carry pointers back to per-open records, so that broadcasting an event to every descriptor becomes a simple list walk. The temptation is understandable; the implementation is full of edge cases. Two threads racing to close the last descriptor while a third is trying to broadcast is the scenario that breaks the straightforward code, and fixing it tends to require more locks than you want to add.

FreeBSD's answer is `devfs_foreach_cdevpriv(9)`, a callback-based iterator that walks the per-open records attached to a given cdev under the right lock. If you ever need the pattern, use that function and give it a callback. Do not maintain your own lists.

We will not use `devfs_foreach_cdevpriv` in Chapter 8. It is named here because if you scan the FreeBSD tree for `cdevpriv` you will find it, and you should recognise it as the safe alternative to reinventing the iteration yourself.



## Destroying cdevs Safely

The act of putting a cdev into devfs is routine. Taking it out again is the part that needs thought. Chapter 7 taught you `destroy_dev(9)`, and for the simple path of a well-behaved driver it is all you need. Real drivers sometimes need more. This section walks through the family of destruction helpers, explains what they guarantee, and shows where each one is the right tool.

### The Draining Model

Let us start from the question destruction has to answer: "when is it safe to free the softc and unload the module?" The naive answer, "after `destroy_dev` returns", is almost right. The careful answer is, "after `destroy_dev` returns **and** no more kernel threads can be in any of my handlers for this cdev."

The `struct cdev` counters you met earlier are how the kernel tracks this. `si_threadcount` increments every time devfs enters one of your handlers on behalf of a user syscall, and decrements every time the handler returns. `destroy_devl`, which is the internal function that `destroy_dev` calls, watches that counter. Here is the relevant excerpt from `/usr/src/sys/kern/kern_conf.c`:

```c
while (csw != NULL && csw->d_purge != NULL && dev->si_threadcount) {
        csw->d_purge(dev);
        mtx_unlock(&cdp->cdp_threadlock);
        msleep(csw, &devmtx, PRIBIO, "devprg", hz/10);
        mtx_lock(&cdp->cdp_threadlock);
        if (dev->si_threadcount)
                printf("Still %lu threads in %s\n",
                    dev->si_threadcount, devtoname(dev));
}
while (dev->si_threadcount != 0) {
        /* Use unique dummy wait ident */
        mtx_unlock(&cdp->cdp_threadlock);
        msleep(&csw, &devmtx, PRIBIO, "devdrn", hz / 10);
        mtx_lock(&cdp->cdp_threadlock);
}
```

Two loops. The first loop calls `d_purge` if the driver provides one; the second simply waits. In both cases the result is the same: `destroy_dev` does not return until `si_threadcount` is zero. This is the **draining** behavior that makes destruction safe. When the call returns, no thread is inside any handler, and no new thread can enter one, because `si_devsw` has been cleared.

What this means for your code: **after `destroy_dev(sc->cdev)` returns, nothing in user space can trigger a call into your handlers with this cdev**. You are free to destroy softc members that those handlers depend on.

### The Four Destruction Functions

FreeBSD exposes four related functions for cdev destruction. Each handles a slightly different case.

**`destroy_dev(struct cdev *dev)`**

The ordinary case. Synchronous: waits for in-flight handlers to finish, then unlinks the cdev from devfs and releases the kernel's primary reference. Used in Chapter 7 and in every single-threaded destruction path in this book. Requires the caller to be sleepable and to not hold any lock that the in-flight handlers might need.

**`destroy_dev_sched(struct cdev *dev)`**

A deferred form. Schedules the destruction on a taskqueue and returns immediately. Useful when the calling context cannot sleep, for instance from inside a callback that runs under a lock. The actual destruction happens asynchronously, and the caller must not assume it has completed when the function returns.

**`destroy_dev_sched_cb(struct cdev *dev, void (*cb)(void *), void *arg)`**

The same deferred form, but with a callback that runs after destruction completes. Use when you need to do follow-up work (free the softc, for example) once you know the cdev is truly gone.

**`destroy_dev_drain(struct cdevsw *csw)`**

The sweep. Waits for **every** cdev registered against the given `cdevsw` to be fully destroyed, including those scheduled through the deferred forms. Used when you are about to unregister or free the `cdevsw` itself, for instance inside the `MOD_UNLOAD` handler of a module that ships multiple drivers.

### The Race That destroy_dev_drain Exists to Prevent

Draining is a subtle point, and the best way to explain it is with the scenario it fixes.

Suppose your module exports a `cdevsw`. In `MOD_UNLOAD`, your code calls `destroy_dev(sc->cdev)` and then returns success. The kernel proceeds to tear the module down. Everything looks fine, until a moment later when a deferred task scheduled earlier through `destroy_dev_sched` finally runs. That task dereferences the `struct cdevsw` as part of its cleanup. The `cdevsw` has been unmapped along with the module. The kernel panics.

The race is narrow but real. `destroy_dev_drain` is the fix: call it on your `cdevsw` after you are confident no new cdevs will be created, and it will not return until every cdev registered against that `cdevsw` has completed its destruction. Only then is it safe to let the module go.

If your driver creates one cdev from `attach`, destroys it from `detach`, and never uses the deferred forms, you do not need `destroy_dev_drain`. `myfirst` does not need it. Real drivers that manage cloning cdevs or that destroy cdevs from event handlers usually do.

### The Order of Operations in detach

Given all this, the right order of operations in a `detach` handler for a driver with a primary cdev, an alias, and per-open state is:

1. Refuse to detach if any descriptors are still open. Return `EBUSY`. Your `active_fhs` counter is the right thing to check.
2. Destroy the alias cdev with `destroy_dev(sc->cdev_alias)`. This unlinks the alias from devfs and drains any in-flight calls against it.
3. Destroy the primary cdev with `destroy_dev(sc->cdev)`. Same as above for the primary.
4. Tear down the sysctl tree with `sysctl_ctx_free(9)`.
5. Destroy the mutex with `mtx_destroy(9)`.
6. Clear the `is_attached` flag in case anything still reads it.
7. Return zero.

Notice that steps 2 and 3 serve two purposes each. They remove the nodes from devfs so no new opens can arrive, and they drain in-flight calls so no handler is still running when step 4 tries to free state that a handler would read.

The pattern is simple. The only way to get it wrong is to free something before the draining `destroy_dev` has finished with it. Stick to this order and you will be safe.

### Unload Under Load

A healthy intuition-building exercise is to reason about what happens when `kldunload` arrives while a userland program is inside a `read(2)` on your device.

Walk through the timeline:

- The kernel begins unloading the module. It calls your `MOD_UNLOAD` handler, which ultimately calls `device_delete_child` on your Newbus device, which invokes your `detach`.
- Your `detach` reaches `destroy_dev(sc->cdev)`. This call is synchronous and will wait for in-flight handlers to finish.
- The userland `read(2)` is currently executing your `d_read`. `si_threadcount` is 1.
- `destroy_dev` sleeps, watching `si_threadcount`.
- Your `d_read` returns. `si_threadcount` drops to 0.
- `destroy_dev` returns. Your `detach` continues with sysctl and mutex teardown.
- The userland `read(2)` has already returned its bytes to user space. The descriptor is still open.
- A subsequent `read(2)` on the same descriptor from the same process now fails cleanly, because the cdev is gone.

This is what "destroy the node first, then tear down its dependencies" buys you. The window where userland could observe inconsistent state is made vanishingly small by the kernel's draining behavior.

### When destroy_dev Refuses to Proceed Quickly

Sometimes `destroy_dev` will sit in its drain loop for longer than you expect. There are a few common reasons.

- A handler is blocked in a sleep that no wakeup is going to release. For instance, a `d_read` that calls `msleep(9)` on a condition variable that nobody ever signals. In that case your driver has a logic bug. Destruction is doing exactly the right thing by refusing to proceed; your job is to wake the blocked thread, either by writing a `d_purge` handler that nudges it or by ensuring the control flow that eventually wakes it is still functional during unload.
- A userland program is stuck in a `ioctl` that is waiting on hardware. The fix is usually the same: a `d_purge` handler that tells the blocked thread to abandon the wait.
- Two destructions racing, each draining the other. That is the case `destroy_dev_drain` exists for.

If you watch `dmesg` during a stuck unload, you will see the `"Still N threads in foo"` message printed by the loop above. That is your cue: find out what those threads are doing and convince them to return.

### A Minimal d_purge Example

For completeness, here is what a simple `d_purge` looks like. Not every driver needs one; it is worth showing so you recognise the shape when you read real code:

```c
static void
myfirst_purge(struct cdev *dev)
{
        struct myfirst_softc *sc = dev->si_drv1;

        mtx_lock(&sc->mtx);
        sc->shutting_down = 1;
        wakeup(&sc->rx_queue);   /* nudge any thread waiting on us */
        mtx_unlock(&sc->mtx);
}

static struct cdevsw myfirst_cdevsw = {
        .d_version = D_VERSION,
        .d_purge   = myfirst_purge,
        /* ... */
};
```

The function is called from inside `destroy_devl` while `si_threadcount` is still non-zero. Its job is to poke any blocked kernel thread inside your handlers so that the thread observes the shutdown and returns. For drivers that do only blocking reads, setting a shutdown flag and issuing a `wakeup(9)` is usually all that is needed. Chapter 10 revisits this when blocking I/O becomes a first-class topic.

### Summary

Destruction of a cdev is a rehearsed dance between three participants: your driver, devfs, and any userland threads currently mid-call. The guarantees are strong if you use the right tools:

- Use `destroy_dev` in ordinary paths. Let it drain.
- Use the deferred forms when you cannot sleep or when you need a callback after destruction.
- Use `destroy_dev_drain` when you are about to free or unregister a `cdevsw`.
- Destroy the node before anything the node's handlers depend on.
- Provide a `d_purge` if your handlers can block indefinitely.

Beyond that, the detail is the kind of thing you look up when you need it. The shape is the part that matters, and the shape is simple.



## Persistent Policy: devfs.conf and devfs.rules

Your driver fixes the **baseline** mode, owner, and group of each node. Persistent operator-side adjustments belong in `/etc/devfs.conf` and `/etc/devfs.rules`. Both files are standard parts of the FreeBSD base system, and both apply to every devfs mount on the host.

### devfs.conf: one-time, per-path adjustments

`devfs.conf` is the simplest tool. Each line applies a one-shot adjustment when a matching device node appears. The format is documented in `devfs.conf(5)`. The common directives are `own`, `perm`, and `link`:

```console
# /etc/devfs.conf
#
# Adjustments applied once when each node appears.

own     myfirst/0       root:operator
perm    myfirst/0       0660
link    myfirst/0       myfirst-primary
```

Those three lines say: every time `/dev/myfirst/0` shows up, chown it to `root:operator`, set its mode to `0660`, and create a symlink named `/dev/myfirst-primary` that points at it. Restart the devfs service to apply changes on a running system:

```sh
% sudo service devfs restart
```

`devfs.conf` is fine for small, stable lab setups. It is not a policy engine. If you need conditional rules or jail-specific filtering, reach for `devfs.rules`.

### devfs.rules: rule-based, used by jails

`devfs.rules` describes named rulesets; each ruleset is a list of patterns and actions. A jail references a ruleset by name in its `jail.conf(5)`, and when the jail's own devfs mount comes up, the kernel walks the matching ruleset and filters the node set accordingly. The format is documented in `devfs(8)` and `devfs.rules(5)`.

A tiny example:

```text
# /etc/devfs.rules

[myfirst_lab=10]
add path 'myfirst/*' unhide
add path 'myfirst/*' mode 0660 group operator
```

This defines a ruleset numbered `10`, named `myfirst_lab`. It unhides any node under `myfirst/` (jails hide nodes by default), and then sets them group-readable and group-writable by `operator`. To use the ruleset, name it in `jail.conf`:

```ini
devfs_ruleset = 10;
```

We are not going to set up a jail in this chapter. The point here is recognition: when you see `devfs_ruleset` in a jail configuration or `service devfs restart` in operator documentation, you are looking at policy that sits on top of whatever your driver exposed, not inside it. Keep your driver honest at the baseline, and let these files shape what the operator allows.

### The Full devfs.conf Syntax

`devfs.conf` has a small, stable grammar. Each line is a directive. Blank lines and lines that begin with `#` are ignored. A `#` anywhere on a line starts a comment to end-of-line. Only three directive keywords exist:

- **`own   path   user[:group]`**: change the ownership of `path` to `user`, and, if `:group` is given, to that group as well. The user and group can be names that exist in the password database or numeric ids.
- **`perm  path   mode`**: change the mode of `path` to the given octal mode. Leading zero is optional but conventional.
- **`link  path   linkname`**: create a symbolic link at `/dev/linkname` pointing at `/dev/path`.

Each directive operates on the node whose path is given relative to `/dev`. The path can name a device directly, or it can name a glob that matches a family of devices. Glob characters are `*`, `?`, and character classes in brackets.

The action is applied when the node first appears under `/dev`. For nodes that exist at boot, that means during the early `service devfs start` phase. For nodes that appear later, as when a driver module loads, the action is applied when the matching cdev is added to devfs.

The effect of `service devfs restart` on a running system is to re-run every directive in `/etc/devfs.conf` against whatever is currently under `/dev`. This is how you apply a newly-added directive to devices that already exist.

### Reading the Example devfs.conf

The FreeBSD base system ships a commented-out example at `/etc/defaults/devfs.conf` (or equivalent path; the file is installed by the base system). The upstream source at `/usr/src/sbin/devfs/devfs.conf` is instructive:

```console
# Commonly used by many ports
#link   cd0     cdrom
#link   cd0     dvd

# Allow a user in the wheel group to query the smb0 device
#perm   smb0    0660

# Allow members of group operator to cat things to the speaker
#own    speaker root:operator
#perm   speaker 0660
```

The file is mostly comments, which is the expected state: FreeBSD installs with no `devfs.conf` directives active. Operators who need host-specific changes add them to `/etc/devfs.conf`. This chapter's Lab 8.4 adds entries for `myfirst/0`.

### Worked Examples That Recur in Practice

Three `devfs.conf` patterns come up often. Each is worth showing once.

**Granting a monitoring tool access to a status node.** Suppose a driver exposes `/dev/something/status` as `root:wheel 0600`, and you want a local monitoring tool that runs as a non-root user to read it. The simplest answer is a dedicated group:

```text
# /etc/devfs.conf
own     something/status        root:monitor
perm    something/status        0640
```

After `service devfs restart`, the node is readable by members of the `monitor` group. Create the group with `pw groupadd monitor` and add the relevant users.

**Providing a stable, convenient name for a renumbered device.** Suppose the driver used to create `/dev/old_name` and now creates `/dev/new_name/0`, and you have scripts that still reference the old path. A `link` directive preserves compatibility:

```text
link    new_name/0      old_name
```

`/dev/old_name` becomes a symbolic link to `/dev/new_name/0`. The link carries no policy of its own; ownership and mode come from the target.

**Widening a narrow default for a lab system.** Suppose a driver defaults to `root:wheel 0600` and you want a lab machine where the local admin user can interact with the node without `sudo`. Rather than modifying the driver, give the local admin the operator group and widen the mode in `devfs.conf`:

```text
own     myfirst/0       root:operator
perm    myfirst/0       0660
```

This is exactly the Lab 8.4 setup. It stays contained to the lab machine and leaves the driver's shipping default intact.

### devfs.rules in Depth

`devfs.rules` is a different beast. Rather than applying one-shot directives to a path, it defines **named rulesets** that a devfs mount can reference. Each ruleset is a list of rules; each rule matches paths by pattern and applies actions.

The file lives at `/etc/devfs.rules` and the base system ships defaults at `/etc/defaults/devfs.rules`. The format is documented in `devfs.rules(5)` and `devfs(8)`.

A ruleset is introduced by a bracketed header:

```text
[rulesetname=number]
```

`number` is a small integer, the way devfs identifies the ruleset internally. `rulesetname` is a human-readable tag for use in jail configuration. Rules following a header belong to that ruleset until the next header.

A rule begins with the `add` keyword and names a path pattern and an action. The common actions are:

- **`unhide`**: make matching nodes visible. Rulesets that derive from `devfsrules_hide_all` use this to whitelist a specific set of nodes.
- **`hide`**: make matching nodes invisible. Used to remove something from the default set.
- **`group name`**: change the group of matching nodes.
- **`user name`**: change the owner.
- **`mode N`**: change the mode to octal `N`.
- **`include $name`**: include the rules from another ruleset named `$name`.

Include directives are how FreeBSD's shipping rulesets compose. The `devfsrules_jail` ruleset begins with `add include $devfsrules_hide_all` to establish a clean slate, then includes `devfsrules_unhide_basic` for the handful of nodes every reasonable program expects, then `devfsrules_unhide_login` for the PTYs and standard descriptors, then adds a few jail-specific paths on top.

### Reading the Default Rulesets

The FreeBSD source ships `/etc/defaults/devfs.rules` (installed from `/usr/src/sbin/devfs/devfs.rules`). Reading it once gives you a model of how rules are layered.

```text
[devfsrules_hide_all=1]
add hide
```

Ruleset 1 hides every node in devfs. It is the starting point for jail rulesets that need to whitelist.

```text
[devfsrules_unhide_basic=2]
add path null unhide
add path zero unhide
add path crypto unhide
add path random unhide
add path urandom unhide
```

Ruleset 2 unhides the basic pseudo-devices every reasonable process expects.

```text
[devfsrules_unhide_login=3]
add path 'ptyp*' unhide
add path 'ptyq*' unhide
/* ... many more PTY paths ... */
add path ptmx unhide
add path pts unhide
add path 'pts/*' unhide
add path fd unhide
add path 'fd/*' unhide
add path stdin unhide
add path stdout unhide
add path stderr unhide
```

Ruleset 3 unhides the TTY and file-descriptor infrastructure that logged-in users depend on.

```text
[devfsrules_jail=4]
add include $devfsrules_hide_all
add include $devfsrules_unhide_basic
add include $devfsrules_unhide_login
add path fuse unhide
add path zfs unhide
```

Ruleset 4 composes the three previous rulesets and adds `fuse` and `zfs`. This is the default ruleset most jails use.

```text
[devfsrules_jail_vnet=5]
add include $devfsrules_hide_all
add include $devfsrules_unhide_basic
add include $devfsrules_unhide_login
add include $devfsrules_jail
add path pf unhide
```

Ruleset 5 is ruleset 4 plus the packet-filter control node. Used by jails that need `pf` to be reachable.

Reading these carefully is a good investment. Every pattern you will need for your own rulesets is in one of them.

### A Complete Jail Example End to End

To ground the theory, here is a complete example that a reader can apply on a lab system. It assumes you have built and loaded the Chapter 8 stage 2 driver and that `/dev/myfirst/0` exists on the host.

**Step 1: define a ruleset in `/etc/devfs.rules`.** Add to the end of the file:

```text
[myfirst_jail=100]
add include $devfsrules_jail
add path 'myfirst'   unhide
add path 'myfirst/*' unhide
add path 'myfirst/*' mode 0660 group operator
```

The ruleset is numbered `100` (any unused small integer works; `100` is safely above the shipped numbers). It includes the default jail ruleset so the jail still has `/dev/null`, `/dev/zero`, the PTYs, and everything else a normal jail needs. Then it unhides the `myfirst/` directory and the nodes inside it, and sets their mode and group.

**Step 2: create a jail.** A minimal `/etc/jail.conf` entry:

```text
myfirstjail {
        path = "/jails/myfirstjail";
        host.hostname = "myfirstjail.example.com";
        mount.devfs;
        devfs_ruleset = 100;
        exec.start = "/bin/sh";
        exec.stop  = "/bin/sh -c 'exit'";
        persist;
}
```

Create the jail root:

```sh
% sudo mkdir -p /jails/myfirstjail
% sudo bsdinstall jail /jails/myfirstjail
```

Replace `bsdinstall` with whatever jail-creation method fits your lab if you already have one.

**Step 3: start the jail and inspect.**

```sh
% sudo service devfs restart
% sudo service jail start myfirstjail
% sudo jexec myfirstjail ls -l /dev/myfirst
total 0
crw-rw----  1 root  operator  0x5a Apr 17 10:00 0
```

The node appears inside the jail with the ruleset-specified ownership and mode. If the ruleset did not unhide it, the jail would see no `myfirst` directory at all.

**Step 4: prove it.** Comment out the `add path 'myfirst/*' unhide` line in `/etc/devfs.rules`, run `sudo service devfs restart`, and re-enter the jail:

```sh
% sudo jexec myfirstjail ls -l /dev/myfirst
ls: /dev/myfirst: No such file or directory
```

The node is invisible to the jail. The host still sees it. The driver has not been reloaded. Policy in the file completely determines what the jail sees.

This end-to-end exercise is what Lab 8.7 walks through. The point of showing it once in prose is to establish the pattern: **rulesets shape what the jail sees, and the driver does nothing different regardless**. Your driver's job is to expose a sound baseline; the rulesets' job is to filter and adjust per environment.

### Debugging Rule Mismatches

When devfs does not present a node the way you expect, there are a few tools for diagnosing why.

- **`devfs rule show`** displays the rulesets currently loaded into the kernel. You can compare them against the file.
- **`devfs rule showsets`** lists the ruleset numbers currently active.
- **`devfs rule ruleset NUM`** followed by **`devfs rule show`** displays rules inside a specific ruleset.
- **`service devfs restart`** reapplies `/etc/devfs.conf` and reloads all rulesets from `/etc/devfs.rules`. Use this any time you change either file.

Common failure modes:

- A rule uses a path pattern that does not match. Single-quote your glob patterns and remember that `myfirst/*` is different from `myfirst` (the directory itself is not covered by the `/*` pattern; you need both rules).
- A ruleset is referenced by a jail but not present in `/etc/devfs.rules`. The jail's `/dev` ends up with no matching rules applied, which often means a hidden-by-default filesystem.
- A ruleset is present but never restarted. After you add a rule, run `service devfs restart` to actually push it into the kernel.

### Runtime Manipulation With devfs(8)

`devfs(8)` is the low-level administrative tool that `service devfs restart` uses under the hood. You can invoke it directly to apply changes without rebooting or restarting the whole devfs subsystem.

```sh
% sudo devfs rule -s 100 add path 'myfirst/*' unhide
```

This adds a rule to ruleset 100 in the running kernel, without touching the file. Useful for experimentation. Rules added this way do not persist across reboots.

```sh
% sudo devfs rule showsets
0 1 2 3 4 5 100
```

Shows which ruleset numbers are currently loaded.

```sh
% sudo devfs rule -s 100 del 1
```

Deletes rule number 1 in ruleset 100.

You will rarely need to touch `devfs(8)` directly in production; the file-based workflow and `service devfs restart` are enough for most needs. During the debugging of a stubborn ruleset, the interactive command is invaluable.

### A Caveat About `devfs.conf` Timing

One pattern that bites newcomers is this: you add a `devfs.conf` line, reboot, and discover the line did not take effect. Usually the reason is ordering. `service devfs start` runs early in the boot sequence, before some modules load. Nodes created later by modules loaded later will not be matched by the already-run directives unless you restart the devfs service after the module loads.

In practice this means:

1. If your driver is compiled into the kernel, its nodes exist at the time `devfs.conf` is applied. The directives take effect on first boot.
2. If your driver is loaded from `/boot/loader.conf`, its nodes exist before userland starts, so directives are applied normally.
3. If your driver is loaded later (from `rc.d` or by hand), you must run `service devfs restart` after the load so the directives apply to the newly-appeared nodes.

For labs, the last case is the common one. Load the driver, run `service devfs restart`, then verify.



## Exercising Your Device From Userland

Shell tools will get you surprisingly far. You already know these from Chapter 7:

```sh
% ls -l /dev/myfirst/0
% sudo cat </dev/myfirst/0
% echo "hello" | sudo tee /dev/myfirst/0 >/dev/null
```

They remain useful, especially `ls -l` for confirming that a permission change took effect. But at some point you will want to open the device from a program you wrote yourself, so you can control timing, measure behavior, and simulate realistic user code. The companion files under `examples/part-02/ch08-working-with-device-files/userland/` contain a small probe program that does exactly that. The relevant pieces look like this:

```c
#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

int
main(int argc, char **argv)
{
        const char *path = (argc > 1) ? argv[1] : "/dev/myfirst/0";
        char buf[64];
        ssize_t n;
        int fd;

        fd = open(path, O_RDWR);
        if (fd < 0)
                err(1, "open %s", path);

        n = read(fd, buf, sizeof(buf));
        if (n < 0)
                err(1, "read %s", path);

        printf("read %zd bytes from %s\n", n, path);

        if (close(fd) != 0)
                err(1, "close %s", path);

        return (0);
}
```

Two things to notice. First, there is nothing device-specific in the code. It is the same `open`, `read`, `close` you would write against a regular file. That is the UNIX tradition paying off. Second, compiling and running this program gives you a repeatable way to drive your driver without shell quoting to worry about. In Chapter 9 you will extend it to write data, measure byte counts, and compare per-open state across descriptors.

Running it once against your Stage 2 driver should produce something like:

```sh
% cc -Wall -Werror -o probe_myfirst probe_myfirst.c
% sudo ./probe_myfirst
read 0 bytes from /dev/myfirst/0
```

Zero bytes, because `d_read` still returns EOF. The number is boring; the fact that the whole path worked is not.

### A Second Probe: Inspecting With stat(2)

Reading the metadata of a device node is just as instructive as opening it. FreeBSD's `stat(1)` command and the `stat(2)` system call both report what devfs is advertising. A tiny program built around `stat(2)` makes it easy to compare a primary node and an alias and confirm that they resolve to the same cdev.

The companion source `examples/part-02/ch08-working-with-device-files/userland/stat_myfirst.c` looks like this:

```c
#include <err.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>

int
main(int argc, char **argv)
{
        struct stat sb;
        int i;

        if (argc < 2) {
                fprintf(stderr, "usage: %s path [path ...]\n", argv[0]);
                return (1);
        }

        for (i = 1; i < argc; i++) {
                if (stat(argv[i], &sb) != 0)
                        err(1, "stat %s", argv[i]);
                printf("%s: mode=%06o uid=%u gid=%u rdev=%#jx\n",
                    argv[i],
                    (unsigned)(sb.st_mode & 07777),
                    (unsigned)sb.st_uid,
                    (unsigned)sb.st_gid,
                    (uintmax_t)sb.st_rdev);
        }
        return (0);
}
```

Running it against both the primary node and the alias should show the same `rdev` on both paths:

```sh
% sudo ./stat_myfirst /dev/myfirst/0 /dev/myfirst
/dev/myfirst/0: mode=020660 uid=0 gid=5 rdev=0x5a
/dev/myfirst:   mode=020660 uid=0 gid=5 rdev=0x5a
```

`rdev` is the identifier devfs uses to tag the node, and it is the simplest proof that two names really refer to the same underlying cdev. The `020000` high bits in the mode say "character special file"; the low bits are the familiar `0660`.

### A Third Probe: Parallel Opens

The stage 2 driver lets multiple processes hold the device open simultaneously, and each gets its own per-open structure. A good way to confirm the wiring is to run a program that opens the node several times from within the same process, holds every descriptor for a moment, and reports what happened.

The companion source `examples/part-02/ch08-working-with-device-files/userland/parallel_probe.c` does exactly that:

```c
#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define MAX_FDS 8

int
main(int argc, char **argv)
{
        const char *path = (argc > 1) ? argv[1] : "/dev/myfirst/0";
        int fds[MAX_FDS];
        int i, n;

        n = (argc > 2) ? atoi(argv[2]) : 4;
        if (n < 1 || n > MAX_FDS)
                errx(1, "count must be 1..%d", MAX_FDS);

        for (i = 0; i < n; i++) {
                fds[i] = open(path, O_RDWR);
                if (fds[i] < 0)
                        err(1, "open %s (fd %d of %d)", path, i + 1, n);
                printf("opened %s as fd %d\n", path, fds[i]);
        }

        printf("holding %d descriptors; press enter to close\n", n);
        (void)getchar();

        for (i = 0; i < n; i++) {
                if (close(fds[i]) != 0)
                        warn("close fd %d", fds[i]);
        }
        return (0);
}
```

Run it and watch `dmesg` at the same time:

```sh
% sudo ./parallel_probe /dev/myfirst/0 4
opened /dev/myfirst/0 as fd 3
opened /dev/myfirst/0 as fd 4
opened /dev/myfirst/0 as fd 5
opened /dev/myfirst/0 as fd 6
holding 4 descriptors; press enter to close
```

You should see four `open via myfirst/0 fh=<ptr> (active=N)` lines in `dmesg`, each with a different pointer. When you press Enter, four `per-open dtor fh=<ptr>` lines follow as each descriptor closes. This is your strongest evidence that per-open state really is per-descriptor.

### A Fourth Probe: Stress Testing

A short stress test exercises the destructor path repeatedly and catches leaks that a single-open test would miss. `examples/part-02/ch08-working-with-device-files/userland/stress_probe.c` loops open-and-close:

```c
#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int
main(int argc, char **argv)
{
        const char *path = (argc > 1) ? argv[1] : "/dev/myfirst/0";
        int iters = (argc > 2) ? atoi(argv[2]) : 1000;
        int i, fd;

        for (i = 0; i < iters; i++) {
                fd = open(path, O_RDWR);
                if (fd < 0)
                        err(1, "open (iter %d)", i);
                if (close(fd) != 0)
                        err(1, "close (iter %d)", i);
        }
        printf("%d iterations completed\n", iters);
        return (0);
}
```

Run it against the loaded driver and then verify that the active-open counter returns to zero:

```sh
% sudo ./stress_probe /dev/myfirst/0 10000
10000 iterations completed
% sysctl dev.myfirst.0.stats.active_fhs
dev.myfirst.0.stats.active_fhs: 0
% sysctl dev.myfirst.0.stats.open_count
dev.myfirst.0.stats.open_count: 10000
```

If `active_fhs` settles above zero after the program exits, your destructor is failing to run on some path and you have a real leak to investigate. If `open_count` matches the iteration count, every open was seen. The stress probe is a blunt instrument but it is fast and catches most common mistakes.

### Observing devd Events

`devd(8)` is the userland daemon that reacts to device events. Each time a cdev appears or disappears, devd receives a notification and can run a configured action. You do not need to configure devd to see its events; you can subscribe to the event stream directly through the `/var/run/devd.pipe` socket.

A short helper, `examples/part-02/ch08-working-with-device-files/userland/devd_watch.sh`, wires this up:

```sh
#!/bin/sh
# Print devd events related to the myfirst driver.
nc -U /var/run/devd.seqpacket.pipe | grep -i 'myfirst'
```

Run this in one terminal, then load and unload the driver in another:

```sh
% sudo sh ./devd_watch.sh &
% sudo kldload ./myfirst.ko
!system=DEVFS subsystem=CDEV type=CREATE cdev=myfirst/0
% sudo kldunload myfirst
!system=DEVFS subsystem=CDEV type=DESTROY cdev=myfirst/0
```

You should see `CREATE` and `DESTROY` notifications with the cdev name. This is how operators build automatic reactions: a `devd` rule in `/etc/devd.conf` can match on these events and run a script in response. Challenge 5 at the end of this chapter asks you to write a minimal `devd.conf` rule that logs `myfirst` events.

### Handling errno From Device Operations

A good userland probe does more than just call `open` and exit. It distinguishes between the different errno values the kernel returns and takes sensible action on each. The probes in this chapter all use `err(3)` to print a readable message and exit, which is right for a small experimental tool. Production userland code over device nodes usually looks more like this:

```c
fd = open(path, O_RDWR);
if (fd < 0) {
        switch (errno) {
        case ENXIO:
                /* Driver not ready yet. Retry or report. */
                warnx("%s: driver not ready", path);
                break;
        case EBUSY:
                /* Node is exclusive and already open. */
                warnx("%s: in use by another process", path);
                break;
        case EACCES:
                /* Permission denied. */
                warnx("%s: permission denied", path);
                break;
        case ENOENT:
                /* Node does not exist. Is the driver loaded? */
                warnx("%s: not present", path);
                break;
        default:
                warn("%s", path);
                break;
        }
        return (1);
}
```

That table of errno values is worth internalising. Chapter 8 Section 13 treats it as a first-class topic, because the same values appear on the driver side and deciding which one to return is a design choice.

### Driving Devices From Shell Scripts

Before you reach for a userland C program, remember that shell tools cover many cases adequately. For `myfirst`, a handful of one-liners are useful:

```sh
# Verify the node exists and report its ownership and mode.
ls -l /dev/myfirst/0

# Open and immediately close the device, for probe purposes.
sudo sh -c ': </dev/myfirst/0'

# Read once from the device and pipe the result into hexdump.
sudo cat /dev/myfirst/0 | hexdump -C | head

# Hold the device open for a few seconds with a background shell.
sudo sh -c 'exec 3</dev/myfirst/0; sleep 5; exec 3<&-'

# Show what processes currently have the device open.
sudo fstat /dev/myfirst/0
```

Each of these is a separate debugging move, and together they make up a useful shell toolkit. When you can express your test in shell, shell is often the fastest path.



## Reading Real FreeBSD Drivers Through a Device-File Lens

Nothing cements the device-file model like reading drivers that have had to solve the same problems you are now solving. This section is a guided tour of three drivers in `/usr/src/sys`. The goal is not to understand each driver end to end. The goal is to see how each of them shapes its device file, so you build a library of patterns in your head.

Each walkthrough follows the same shape: open the file, find the `cdevsw`, find the `make_dev` call, find the `destroy_dev` call, notice what is idiomatic and what is unusual.

### Walkthrough 1: /usr/src/sys/dev/null/null.c

The `null` module is the smallest good example in the tree. Open it in an editor. It is short enough to read in a single pass.

What to notice first: there are **three** `cdevsw` structures in one file.

```c
static struct cdevsw full_cdevsw = {
        .d_version =    D_VERSION,
        .d_read =       zero_read,
        .d_write =      full_write,
        .d_ioctl =      zero_ioctl,
        .d_name =       "full",
};

static struct cdevsw null_cdevsw = {
        .d_version =    D_VERSION,
        .d_read =       (d_read_t *)nullop,
        .d_write =      null_write,
        .d_ioctl =      null_ioctl,
        .d_name =       "null",
};

static struct cdevsw zero_cdevsw = {
        .d_version =    D_VERSION,
        .d_read =       zero_read,
        .d_write =      null_write,
        .d_ioctl =      zero_ioctl,
        .d_name =       "zero",
        .d_flags =      D_MMAP_ANON,
};
```

Three distinct nodes, three distinct `cdevsw` values, no softc. The module registers three cdevs in its `MOD_LOAD` handler:

```c
full_dev = make_dev_credf(MAKEDEV_ETERNAL_KLD, &full_cdevsw, 0,
    NULL, UID_ROOT, GID_WHEEL, 0666, "full");
null_dev = make_dev_credf(MAKEDEV_ETERNAL_KLD, &null_cdevsw, 0,
    NULL, UID_ROOT, GID_WHEEL, 0666, "null");
zero_dev = make_dev_credf(MAKEDEV_ETERNAL_KLD, &zero_cdevsw, 0,
    NULL, UID_ROOT, GID_WHEEL, 0666, "zero");
```

Notice `MAKEDEV_ETERNAL_KLD`. When this code is statically compiled into the kernel, the macro expands to `MAKEDEV_ETERNAL` and marks the cdevs as never-to-be-destroyed. When the same code is built as a loadable module, the macro expands to zero, and the cdevs can be destroyed during unload.

Also notice that mode `0666` and `root:wheel`. Everything the null module serves is intentionally accessible to everyone.

Unload reads as simply as load:

```c
destroy_dev(full_dev);
destroy_dev(null_dev);
destroy_dev(zero_dev);
```

One `destroy_dev` per cdev. No softc to tear down. No per-open state. No locking beyond what the kernel provides. This is what minimal looks like.

**What to copy from null:** the habit of setting `d_version`, the habit of giving each `cdevsw` its own `d_name`, the symmetry between load and unload, the willingness to use simple named handlers instead of inventing abstractions.

**What to leave alone:** `MAKEDEV_ETERNAL_KLD`. Your driver should be unloadable, so you do not want the eternal flag. The `null` module is special because the nodes it creates predate almost every other kernel subsystem and are expected to remain live for the life of the kernel.

### Walkthrough 2: /usr/src/sys/dev/led/led.c

The LED framework is a step up in structural complexity. It is still small enough to read in a sitting. Where `null` has no softc, `led` has a full per-LED softc. Where `null` creates three singletons, `led` creates one cdev per LED on demand.

Look first at the single `cdevsw`:

```c
static struct cdevsw led_cdevsw = {
        .d_version =    D_VERSION,
        .d_write =      led_write,
        .d_name =       "LED",
};
```

One `cdevsw` for all LEDs. The framework uses it for every cdev it creates, relying on `si_drv1` to distinguish between them. The minimalism of this definition is itself a lesson: `led` does not implement `d_open`, `d_close`, or `d_read`, because every interaction an operator has with a LED is a pattern string written with `echo`. Reading from the node is not meaningful, and no session state needs to be tracked on open, so the driver simply leaves those fields unset. devfs interprets each `NULL` slot as "use the default behaviour", which for `d_read` is returning zero bytes and for `d_open` and `d_close` is doing nothing. Keep that in mind when you design your own `cdevsw` values: populate what your device truly needs, and leave the rest alone.

The per-LED softc lives in a `struct ledsc` defined near the top of the file:

```c
struct ledsc {
        LIST_ENTRY(ledsc)       list;
        char                    *name;
        void                    *private;
        int                     unit;
        led_t                   *func;
        struct cdev             *dev;
        /* ... more state ... */
};
```

It carries a back-pointer to its cdev in the `dev` field, and a unit number allocated from an `unrhdr(9)` pool rather than from Newbus:

```c
sc->unit = alloc_unr(led_unit);
```

The actual `make_dev` call is right below:

```c
sc->dev = make_dev(&led_cdevsw, sc->unit,
    UID_ROOT, GID_WHEEL, 0600, "led/%s", name);
```

Notice the path: `"led/%s"`. Every LED created by the framework lands in the `/dev/led/` subdirectory with a free-form name chosen by the calling driver (for instance `led/ehci0`). This is how the framework keeps its nodes grouped.

Immediately after the `make_dev`, the framework stashes the softc pointer:

```c
sc->dev->si_drv1 = sc;
```

This is the pre-`mda_si_drv1` way of doing it, and it predates `make_dev_s`. Newer drivers should pass `mda_si_drv1` through the args struct instead, so the pointer is set before the cdev becomes reachable.

Destruction is a single call:

```c
destroy_dev(dev);
```

Simple. Synchronous. No deferred destruction, no drain loop at the caller level. The framework relies on the kernel's draining behavior in `destroy_dev` to finish any in-flight handlers.

**What to copy from led:** the naming convention (subdirectory per framework), the softc layout (back-pointer plus identity fields plus callback pointer), the clean `alloc_unr` / `free_unr` pattern for unit numbers that do not come from Newbus.

**What to leave alone:** the `sc->dev->si_drv1 = sc` assignment after `make_dev`. Use `mda_si_drv1` in `make_dev_s` instead.

### Walkthrough 3: /usr/src/sys/dev/md/md.c

The memory disk driver is larger than the previous two, and most of its bulk is not about device files at all. It is about GEOM, about backing store, about swap-backed and vnode-backed instances. For our purposes, we look at one specific thing: the control node `/dev/mdctl`.

Find the `cdevsw` declaration near the top of `md.c`:

```c
static struct cdevsw mdctl_cdevsw = {
        .d_version =    D_VERSION,
        .d_ioctl =      mdctlioctl,
        .d_name =       MD_NAME,
};
```

Only two fields set. `d_version` and `d_ioctl` and a name. No `d_open`, `d_close`, `d_read`, or `d_write`. The control node is used exclusively through `ioctl(2)`: create an md, attach a backing store, destroy an md. That is the shape of many control interfaces in the tree.

The cdev is created near the bottom of the file:

```c
status_dev = make_dev(&mdctl_cdevsw, INT_MAX, UID_ROOT, GID_WHEEL,
    0600, MDCTL_NAME);
```

`INT_MAX` is a common pattern for singletons when the unit number does not matter: it places the cdev outside any plausible range of driver-instance unit numbers. `0600` and `root:wheel` are the narrow baseline you would expect for a privileged control node.

Destruction happens in the module unload path:

```c
destroy_dev(status_dev);
```

Again, a single call.

**What to copy from md:** the pattern of exposing a single control cdev for a subsystem whose data path lives elsewhere (in md's case, in GEOM), and the very narrow permissions for a node that has genuine privilege.

**What to leave alone:** `md` is a large subsystem; do not try to copy its structure as a template. Copy the control-node idea; leave the GEOM layering for Chapter 27.

### A Brief Word About Cloning

The LED framework creates one cdev per LED in response to an API call from other drivers. The `md` module creates one cdev per memory disk in response to an `ioctl` on its control node. Both are examples of **drivers that create cdevs dynamically in response to events**.

FreeBSD also has a dedicated cloning mechanism: `clone_create(9)` and the `dev_clone` event handler. When a user opens a name that matches a pattern registered by a cloning driver, the kernel synthesises a new cdev, opens it, and returns the descriptor. This was historically a common pattern for subsystems that wanted a new per-session cdev on every open. Modern FreeBSD leans on `devfs_set_cdevpriv(9)` instead whenever the only reason to clone a cdev was to give each descriptor independent state, because per-open state is simpler, lighter, and does not need a pool of minor numbers. True cloning is still in the tree (the PTY subsystem under `/dev/pts/*` is the clearest living example), and the API surface that supports it is worth recognising.

Cloning is flexible and we will not cover it in Chapter 8. Several of the API elements we have already met (the `D_NEEDMINOR` flag, `dev_stdclone`, the `CLONE_*` constants) exist to support it. Mentioning them here gives you a vocabulary for the day you encounter a driver that uses them. For now, the takeaway is that FreeBSD has a spectrum of mechanisms for creating cdevs, and `make_dev_s` from `attach` is the simplest end of it.

### Walkthrough 4: /usr/src/sys/net/bpf.c (Device-File Surface Only)

BPF, the Berkeley Packet Filter, is a large subsystem. We will not try to understand what it does at the network level; Part 6 of this book has a chapter dedicated to network drivers. Here we look at one specific thing: how BPF shapes its device-file surface.

The relevant declarations are near the top of `/usr/src/sys/net/bpf.c`. The `cdevsw` is populated with the full set of data-path operations plus poll and kqueue:

```c
static struct cdevsw bpf_cdevsw = {
        .d_version =    D_VERSION,
        .d_open =       bpfopen,
        .d_read =       bpfread,
        .d_write =      bpfwrite,
        .d_ioctl =      bpfioctl,
        .d_poll =       bpfpoll,
        .d_name =       "bpf",
        .d_kqfilter =   bpfkqfilter,
};
```

No `d_close`. BPF does everything on close through the cdevpriv destructor, which is the pattern this chapter recommends. The dispatch table says "for these operations, call these handlers, and do nothing special on close because the destructor has it".

The open handler, already quoted in the per-open state section, fits the pattern exactly:

```c
d = malloc(sizeof(*d), M_BPF, M_WAITOK | M_ZERO);
error = devfs_set_cdevpriv(d, bpf_dtor);
if (error != 0) {
        free(d, M_BPF);
        return (error);
}
```

Allocate, register with devfs, free on registration failure. Nothing else in the open path matters for our purposes here.

The destructor is the interesting part. BPF's `bpf_dtor` has to unwind a fair amount of state: it stops a callout, disconnects from its BPF interface, drains a select set, and drops references. It does all of that **without ever calling `d_close`** from the cdevsw. Destructor-based cleanup is cleaner than close-based cleanup for a driver that supports multiple openers, because the destructor fires exactly once per open, whereas `d_close` without `D_TRACKCLOSE` fires only on the last close of the final shared file.

The cdev layout on the BPF side is simpler than a first reading of the subsystem suggests. BPF creates exactly one primary node and one alias:

```c
dev = make_dev(&bpf_cdevsw, 0, UID_ROOT, GID_WHEEL, 0600, "bpf");
make_dev_alias(dev, "bpf0");
```

That is the full register-a-device surface. There is no per-instance `/dev/bpfN` cdev; the names you may see on a running system such as `/dev/bpf0` and `/dev/bpf` both resolve to the same cdev, and every distinct user of BPF is distinguished entirely by its per-open structure, attached through `devfs_set_cdevpriv` at open time. That is exactly the shape you adopt for `myfirst` at the end of this chapter: one cdev, an alias, per-descriptor state, and a destructor that handles teardown. BPF is a large subsystem, but its device-file surface is one you now know how to write.

**What to copy from BPF:** the practice of doing open-time cleanup in the destructor rather than in `d_close`. The practice of allocating per-open state immediately and registering it before any other work. The discipline of freeing the allocation on registration failure. The willingness to keep the cdev count small and let the descriptor carry the session.

**What to leave alone:** the BPF-specific machinery that surrounds the open and close paths, including the interface attach logic, the select-set bookkeeping, and the counter-based statistics. Those belong to the network side of BPF, and Part 6 will revisit them when the book introduces network drivers.

### A Synthesis Across All Four Drivers

With four walkthroughs under your belt, it helps to line up the commonalities and the differences in one table. Every row is a driver property; every column is a driver.

| Property                  | `null`       | `led`          | `md`             | `bpf`             |
|---------------------------|--------------|----------------|------------------|-------------------|
| How many `cdevsw` values  | 3            | 1              | 1 (plus GEOM)    | 1                 |
| cdevs per attach          | 3 total      | 1 per LED      | 1 control + many | 1 plus 1 alias    |
| Softc?                    | no           | yes            | yes              | yes (per-open)    |
| Subdirectory in /dev?     | no           | yes (`led/*`)  | no               | no                |
| Permission mode           | `0666`       | `0600`         | `0600`           | `0600`            |
| Uses `devfs_set_cdevpriv`?| no           | no             | no               | yes               |
| Uses cloning?             | no           | no             | no               | no                |
| Uses `make_dev_alias`?    | no           | no             | no               | yes               |
| `d_close` populated?      | no           | no             | no               | no                |
| Uses `destroy_dev_drain`? | no           | no             | no               | no                |
| Primary use-case          | pseudo-data  | hardware ctl   | subsystem ctl    | packet capture    |

Every column is defensible. Each driver has chosen the simplest set of features that fits its job. Your `myfirst` driver by the end of Chapter 8 has a profile closer to `led` than to the others: one `cdevsw`, per-instance softc, subdirectory naming, narrow permissions, plus `devfs_set_cdevpriv` for per-open state (which `led` does not need) and an alias (which `led` does not use).

That profile is a good place to be. It is large enough to show that you have engaged with the real mechanisms, and small enough that every line of the driver is there for a reason.

### What Four Drivers Taught Us

Four drivers, four different shapes, all of them good examples:

- `null` is minimalism: three `cdevsw` values, three singletons, no softc, no per-open state, mode `0666` because the data is harmless.
- `led` is a framework: one `cdevsw`, many cdevs, each with its own softc and unit number, subdirectory naming, narrow permissions, only `d_write` populated because the device is written-to rather than read-from.
- `md` is a control interface: one `cdevsw` with only `d_ioctl`, a singleton cdev, `INT_MAX` as unit number, narrow permissions for privileged operations.
- `bpf` is a per-session driver: one `cdevsw` with the full data-path set, a single primary cdev plus an alias, and the whole of per-descriptor state carried through `devfs_set_cdevpriv(9)`.

Your `myfirst` is closest to `bpf` in shape by the end of this chapter: one `cdevsw`, one primary cdev with an alias, a softc for device-wide state, and per-open state for per-descriptor book keeping. Where it differs is scope. `bpf` implements the full data path, and it does so to serve real packets. `myfirst` stops at the surface. That is fine for now. You are in good company, and the rooms behind the door open in Chapter 9.

Reading other drivers becomes a skill you build over time. The device-file lens is just one of several lenses you will apply. Keep the four walkthroughs above in mind as a starting library.



## Common Scenarios and Recipes

This section is a cookbook. Each entry is a situation you will run into sooner or later, a short recipe for handling it, and a pointer to which part of the chapter explains the underlying machinery. Read them now or bookmark them for when they come up.

### Recipe 1: A Driver That Must Never Be Opened Twice

**Situation.** Your hardware has state that is only sane with a single concurrent user. A second `open(2)` would corrupt the state.

**Recipe.**

1. In the softc, keep a single `int is_open` flag and a mutex.
2. In `d_open`, take the mutex, check the flag, return `EBUSY` if set, set it, drop the mutex.
3. In `d_close`, take the mutex, clear the flag, drop the mutex.
4. In `detach`, check the flag under the mutex and return `EBUSY` if set.
5. Do **not** also use `devfs_set_cdevpriv` for per-open state; there is no "per-open" because there is only one open at a time.

This is the Chapter 7 pattern. `myfirst` used it to force attention on the lifecycle; Chapter 8 stage 2 moves away from it because most drivers want multiple openers. Use the exclusive pattern only when hardware constraints force it.

### Recipe 2: A Driver With Per-User Read Offsets

**Situation.** Your device exposes a seekable stream. Two different user processes should each get their own offset into the stream; neither should see the other's position change their own view.

**Recipe.**

1. Define `struct myfirst_fh` with an `off_t read_offset` field.
2. In `d_open`, allocate the struct and call `devfs_set_cdevpriv` with a destructor that frees it.
3. In `d_read`, call `devfs_get_cdevpriv` to retrieve the struct. Use `fh->read_offset` as the starting point. Advance it by the number of bytes actually transferred.
4. In `d_close`, do nothing; the destructor runs automatically when the descriptor releases.

Chapter 9 will fill in the `d_read` body. The skeleton is already in stage 2.

### Recipe 3: A Device With a Privileged Control Node

**Situation.** Your driver has a data surface anyone should be able to read, and a control surface only privileged users should touch.

**Recipe.**

1. Define two `cdevsw` structures, `something_cdevsw` and `something_ctl_cdevsw`.
2. Create two cdevs in `attach`: one for the data node (`0644 root:wheel` or similar), one for the control node (`0600 root:wheel`).
3. Keep both pointers in the softc. Destroy the control cdev before the data cdev in `detach`.
4. Use `priv_check(9)` inside the control node's `d_ioctl` handler if you want enforcement beyond file permissions.

Lab 8.5 walks through this. The `md` walkthrough in the Reading Real Drivers section is a real-world variant.

### Recipe 4: A Node That Appears Only When a Condition Is Met

**Situation.** You want `/dev/myfirst/status` to appear only when the driver's hardware is in a specific state, and disappear otherwise.

**Recipe.**

1. Keep `sc->cdev_status` as a `struct cdev *` in the softc, initialised to `NULL`.
2. In the handler where the condition becomes true, call `make_dev_s` and store the pointer.
3. In the handler where the condition becomes false, call `destroy_dev` on the pointer and set it to `NULL`.
4. Guard both with the softc mutex so concurrent transitions do not race.
5. In `detach`, if the pointer is non-NULL, destroy it before tearing down the other cdevs.

This is the same pattern as the primary data node, just triggered by a different event. Watch for the case where the condition changes repeatedly in quick succession: you will hit the devfs allocator harder than expected, and you may want to debounce.

### Recipe 5: A Node Whose Ownership Should Change Based on a Runtime Knob

**Situation.** A lab system sometimes wants the node owned by `operator`, sometimes by `wheel`. You do not want to reload the driver to switch.

**Recipe.**

1. Leave the driver's `mda_uid` and `mda_gid` at a narrow baseline (`UID_ROOT`, `GID_WHEEL`).
2. Use `devfs.conf` to widen when wanted:

   ```
   own     myfirst/0       root:operator
   perm    myfirst/0       0660
   ```

3. Apply with `service devfs restart`.
4. To switch back, comment out the lines and re-run `service devfs restart`.

Policy lives in userland; the driver stays pristine. Lab 8.4 practices this.

### Recipe 6: A Node That Should Be Visible Only Inside a Jail

**Situation.** A node should appear inside a specific jail but not on the host.

**Recipe.**

1. In the driver, default to creating the node as usual on the host.
2. In `/etc/devfs.rules`, create a ruleset for the jail that explicitly hides the node:

   ```
   [special_jail=120]
   add include $devfsrules_hide_all
   add include $devfsrules_unhide_basic
   add path myfirst hide
   ```

3. Apply the ruleset number in the jail's `jail.conf`.

This is a blunter variant of Lab 8.7. For a node that should be visible in the jail and not on the host, the logic inverts: create the node on the host (where it is always created), and use a host-side `devfs.conf` rule to tighten permissions so nothing on the host touches it.

### Recipe 7: A Node That Can Survive Unexpected Process Deaths

**Situation.** Your driver holds resources per open. If a process crashes, you must not leak the resource.

**Recipe.**

1. Allocate the resource in `d_open`.
2. Register a destructor with `devfs_set_cdevpriv`. The destructor frees the resource.
3. Rely on the kernel to run the destructor when the last reference to the `struct file` drops, regardless of why. `close(2)`, `exit(3)`, or SIGKILL all reach the same cleanup path.

The destructor guarantee is the primary reason `devfs_set_cdevpriv` exists. No matter how badly userland misbehaves, your resource gets freed.

### Recipe 8: A Device That Must Support Polling

**Situation.** Your driver produces events, and user programs want to `select(2)` or `poll(2)` on the device to learn when an event is pending.

**Recipe.** Out of scope for Chapter 8; this is Chapter 10 territory. The short form for recognition: set `.d_poll = myfirst_poll` in your `cdevsw`, implement the handler to return the appropriate mask of `POLLIN`, `POLLOUT`, `POLLERR` bits, and use `selrecord(9)` to register interest for deferred wake-up. Chapter 10 walks through each of these in detail.

### Recipe 9: A Device That Needs to Be Mapped Into User Memory

**Situation.** Your driver has a shared memory region (DMA buffer, hardware register window) that a user process should access directly through `mmap(2)`.

**Recipe.** Out of scope for Chapter 8; covered in Part 4 when hardware access is introduced. For recognition only: set `.d_mmap = myfirst_mmap` in your `cdevsw`, implement the handler to return the physical page address for each offset and protection mask, and think carefully about what happens when the user-mapped memory is backed by hardware that can vanish. This is one of the more nuanced areas of driver work.

### Recipe 10: A Device That Exposes a Log

**Situation.** Your driver produces log messages that are more voluminous than `device_printf` should handle, and user programs should be able to `read(2)` them in order.

**Recipe.**

1. Allocate a ring buffer in the softc.
2. In the code paths that produce log events, format them into the ring buffer under a lock.
3. In `d_read`, copy bytes from the ring buffer to userland via `uiomove(9)` (Chapter 9).
4. Use per-open state (a `read_offset` in the `fh`) so each reader drains the buffer at its own pace.
5. Consider setting `D_TRACKCLOSE` if a single reader should be allowed to empty the buffer on close.

This is the shape of several kernel log devices. It is worth knowing the pattern exists; the full implementation is a later-chapter exercise.

### When a Recipe Does Not Fit

The cookbook is not exhaustive. When you face a situation that does not match any of the recipes above, a useful habit is to ask three questions in order:

- **Is it about identity?** Then it is a `cdev` question: naming, subdirectories, aliases, creation, destruction.
- **Is it about policy?** Then it is a permissions and policy question: ownership, mode, `devfs.conf`, `devfs.rules`.
- **Is it about state?** Then it is a per-open versus per-device question: softc or `devfs_set_cdevpriv`.

Most real-world driver design questions resolve into one of these three. When you have classified the question, the rest of the chapter tells you which tool to reach for.



## Practical Workflows for the Device-File Surface

Knowing the APIs is half the job. Knowing when to reach for which one, and how to spot problems quickly, is the other half. This section collects the workflows that will make the next several chapters go smoothly: the inner loop for editing a driver, the habits that catch bugs early, and the checklists worth running through before a serious change.

### The Inner Loop

The "inner loop" is the cycle of edit, build, load, test, unload, edit again. Your Chapter 7 scripts already have a version of this. For Chapter 8 the inner loop gets a little richer because there are more user-visible surfaces to verify.

A useful sequence when you are working on a stage of `myfirst`:

```sh
% cd ~/drivers/myfirst
% sudo kldunload myfirst 2>/dev/null || true
% make clean && make
% sudo kldload ./myfirst.ko
% dmesg | tail -5
% ls -l /dev/myfirst /dev/myfirst/0 2>/dev/null
% sysctl dev.myfirst.0.stats
% sudo ./probe_myfirst /dev/myfirst/0
% sudo kldunload myfirst
% dmesg | tail -3
```

Each line has a purpose. The first unload is defensive: the previous test left the module loaded, and this clears the slate. The `make clean && make` rebuilds from scratch to avoid a stale object file. The first `dmesg | tail -5` shows the attach messages. The `ls -l` and `sysctl` confirm that the user-visible surface is present and the internal counters are initialised. The probe exercises the data path. The final unload and `dmesg` confirm the detach messages.

If any step produces an unexpected result, you know which step. That is the value of scripting the loop: not to save typing, but to make the failure signal unambiguous.

The `rebuild.sh` helper shipped with Chapter 7's examples wraps most of this for you. Lab 8 reuses it unchanged.

### Reading dmesg Well

`dmesg` is the narrative of what your driver did. Reading it well is a habit worth building early.

The kernel's default ring buffer can show tens of thousands of lines from earlier boot and runtime activity. When you are developing a specific driver, three techniques make the relevant slice visible:

**Clearing before the test.** `sudo dmesg -c > /dev/null` clears the buffer. The next load/unload cycle then produces a small, focused log. Use this between experiments.

**Filtering by tag.** `dmesg | grep myfirst` narrows the view to lines your driver produced, assuming your `device_printf` calls emit the driver name. They do, because `device_printf(9)` prefixes every line with the Newbus name of the device.

**Watching in real time.** Run `tail -f /var/log/messages` in a second terminal. Every driver message that makes it to `dmesg` also appears there, with timestamps. This is especially useful during long-running tests such as Lab 8.6's parallel-probe exercise.

### Watching fstat

For detach problems, `fstat(1)` is your friend. Two idioms come up often:

```sh
% fstat /dev/myfirst/0
```

Plain lookup; shows all processes holding the node open. The output columns are user, command, pid, fd, mount, inum, mode, rdev, r/w, name.

```sh
% fstat -p $$ | grep myfirst
```

Restrict the search to the current shell. Useful when you are not sure whether your current shell has a leftover descriptor open from an earlier test.

```sh
% fstat -u $USER | grep myfirst
```

Restrict to your own user's processes. Similar use case, broader scope.

### The sysctl Partner of Every Driver

From Chapter 7 your driver already exposes a sysctl tree under `dev.myfirst.0.stats`. Chapter 8 stage 2 adds `active_fhs` to that tree. When you are running experiments, the sysctls are the cheapest possible observation tool:

```sh
% sysctl dev.myfirst.0.stats
dev.myfirst.0.stats.attach_ticks: 123456
dev.myfirst.0.stats.open_count: 42
dev.myfirst.0.stats.active_fhs: 0
dev.myfirst.0.stats.bytes_read: 0
```

Every counter is a check on what the driver thinks is true. Discrepancies between what you expected and what sysctl shows are always a signal. If `active_fhs` is non-zero when no descriptor should be open, you have a leak. If `open_count` is smaller than the number of times you opened the device, your attach path is running twice or your counter is racy.

Sysctls are cheaper than any other observation mechanism. Prefer them over reading the device itself whenever a numeric or short-string piece of information will do.

### A Quick Checklist for Every Code Change

Before you commit a change to the driver, walk through the following. Ten minutes spent here saves hours of debugging later.

1. Does the driver still build from a clean tree?
2. Does it still load and unload cleanly on a system with no open descriptors?
3. Does the user-visible surface (`ls -l /dev/myfirst/...`) match what your code intends?
4. Are the permissions still the narrow defaults they should be?
5. If you changed `attach`, does every error path still unwind completely?
6. If you changed `detach`, does the driver still unload cleanly when descriptors are held open (either with a clean `EBUSY` return or, if you chose a different policy, without leaking)?
7. If you changed per-open state, do all three of `stress_probe`, `parallel_probe`, and `hold_myfirst` still behave as expected?
8. Did you introduce any `device_printf` calls that should be `if (bootverbose)`-gated so they do not flood the log?
9. Did you leave any `#if 0` or debug prints in place? Pull them out now.
10. If you changed ownership or mode, does `devfs.conf` still produce the expected override?

This checklist is deliberately boring. That is the point. A boring, reliable process beats a heroic debugging session every time.

### A Quick Checklist Before a Release

When you are preparing a stage of your driver for "done", the checklist gets a little longer. All of the above, plus:

1. `make clean && make` from a truly clean tree builds without warnings.
2. `kldload ./myfirst.ko; sleep 0.1; kldunload myfirst` completes ten times without issue.
3. `stress_probe 10000` completes without issue and `active_fhs` returns to zero.
4. `parallel_probe 8` opens eight descriptors, holds, and cleanly closes. Kernel log shows eight distinct `fh=` pointers and eight destructors.
5. `kldunload` while a descriptor is open returns `EBUSY` cleanly, not a panic.
6. `devfs.conf` with a widening entry applies on `service devfs restart`.
7. An `ls -l` audit of `/dev/myfirst*` shows no unexpected modes or ownerships.
8. `dmesg` contains exactly the attach and detach messages you expect, with no warnings or errors.
9. Source code is free of commented-out experiments, TODO lines, and debug helpers.
10. The sysctls your driver exposes are descriptive and documented in code comments.

No commit should skip items 1 through 3. They are the cheapest high-value insurance you can buy.

### A Workflow for Adding a New Node

Walking through the workflow end-to-end once will anchor the earlier sections. Suppose you decide `myfirst` should expose an additional read-only status node at `/dev/myfirst/status`, distinct from the numbered data nodes. Here is how you would do it.

**Step 1: design.** Decide on the shape of the node. Does it belong in the same `cdevsw` as the data node, or a different one? A status-only node that answers `read(2)` with a short text summary usually wants its own `cdevsw` with only `d_read` set, because the policy is different from the data node. Decide on the permission mode. Read-only by anyone suggests `0444`; read-only by operators suggests `0440` with an appropriate group.

**Step 2: declare.** Add the new `cdevsw`, its `d_read` handler, and a `struct cdev *cdev_status` field to the softc.

**Step 3: implement.** Write the `d_read` handler. It formats a short string based on softc state and returns it through `uiomove(9)`. For Chapter 8 you might stub this and fill it in after Chapter 9.

**Step 4: wire.** In `attach`, add the `make_dev_s` call for the status node. In `detach`, add the `destroy_dev` call, before the data node's destroy.

**Step 5: test.** Rebuild, reload, inspect, exercise, unload. Check that `ls -l` shows the status node with the expected mode. Check that `cat /dev/myfirst/status` works and produces sensible output. Check that the whole driver still unloads cleanly.

**Step 6: document.** Add a comment in the driver source describing the node. Add an entry in the `dev.myfirst.0.stats` sysctl if the status is numeric and would fit there too. Note the change in whatever change log you keep.

Six steps, each one small, each one specific. That is the level of granularity at which bugs stay visible.

### A Workflow for Diagnosing a Missing Node

The Chapter 8 troubleshooting walkthrough in the Tools section gave you a short checklist. Here is a fuller workflow that fits on an index card.

**Phase 1: is it the module?**

- `kldstat | grep myfirst` shows the module.
- `dmesg | grep myfirst` shows the attach messages.

If the module is not loaded or the attach did not run, fix that first.

**Phase 2: is it Newbus?**

- `devinfo -v | grep myfirst` shows the Newbus device.

If Newbus shows nothing, your `device_identify` or `device_probe` is not creating the child. Look there.

**Phase 3: is it devfs?**

- `ls -l /dev/myfirst` lists the directory (or reports it missing).
- `dmesg | grep 'make_dev'` shows any failure from `make_dev_s`.

If Newbus is fine but devfs shows nothing, `make_dev_s` returned an error. Check your path format string, your `mda_devsw`, your argument structure.

**Phase 4: is it policy?**

- `devfs rule showsets` lists active rulesets.
- `devfs rule -s N show` lists rules in ruleset N.

If devfs has the cdev but your jail or your local session does not see it, the ruleset is hiding it.

Every failure maps to one of these four phases. Work through them in order and you will almost always identify the cause in under a minute.

### A Workflow for Reviewing Someone Else's Driver

When you review a pull request that touches a driver's device-file surface, the useful questions are:

- Does every `make_dev_s` have a matching `destroy_dev`?
- Does every error path after `make_dev_s` call `destroy_dev` before returning?
- Does `detach` destroy every cdev it created?
- Is `si_drv1` populated through `mda_si_drv1` rather than a post-hoc assignment?
- Is the permission mode defensible for the node's purpose?
- Is the cdevsw's `d_version` set to `D_VERSION`?
- Are all `d_*` handlers present for the operations the node should support, and is each of them consistent about its errno returns?
- If the driver uses `devfs_set_cdevpriv`, is there exactly one successful set per open and exactly one destructor?
- If the driver uses aliases, are they destroyed in `detach` before the primary?
- If the driver has more than one cdev, does it call `destroy_dev_drain` in its unload path?

This is a review checklist, not a tutorial. The review is faster because every question has a yes or no answer and every yes can be checked mechanically.

### Keeping a Lab Logbook

A lab logbook is a small notebook or text file where you record what you did, what you saw, and what you learned. The book has recommended this since Chapter 2. In Chapter 8 it pays off in a specific way: you will run the same kinds of experiments many times, and a brief note lets you avoid repeating the same misstep twice.

A useful template for a logbook entry:

```text
Date: 2026-04-17
Driver: myfirst stage 2
Goal: verify per-open state is isolated across two processes
Steps:
 - loaded stage 2 kmod
 - ran parallel_probe with count=4
 - observed 4 distinct fh= pointers in dmesg
 - observed active_fhs=4 in sysctl
 - closed, observed 4 destructor lines, active_fhs=0
Result: as expected
Notes: first run missed destructor lines because dmesg ring buffer
       was full; dmesg -c before the test solved it
```

Two minutes per experiment, no more. The value appears months later when you are tracking down a new issue and a logbook search reveals that the same symptom appeared once before under different circumstances.

### Common Design Questions and How to Think About Them

Some questions recur when driver authors reach the device-file stage. Each of these has come up more than once in real review discussions. The answers are short; the reasoning is worth internalising.

**Q: Should I create the cdev in `device_identify` or in `device_attach`?**

In `device_attach`. The `identify` callback runs very early, before the driver instance has a softc. The cdev wants to reference the softc via `mda_si_drv1`, which means the softc must already exist. Chapter 7 set this pattern; keep it.

**Q: Should I create additional cdevs outside `attach` and `detach`?**

If they are genuinely per-driver-instance, put them in `attach` and destroy them in `detach`. If they are dynamic, created in response to a user action, create them in whichever handler receives the user's request and destroy them either when a later handler unwinds the request or when the driver detaches. Track them carefully; lost cdevs are a common source of leaks.

**Q: Should I set `D_TRACKCLOSE`?**

Usually no. The per-open state mechanism via `devfs_set_cdevpriv` covers almost every case for which `D_TRACKCLOSE` would be tempting, and it cleans itself up automatically. Set `D_TRACKCLOSE` only when you need your `d_close` to run on every descriptor close, not just the last one. Real use cases are rare; TTY drivers and a handful of others fit.

**Q: Should I allow multiple openers?**

Default to yes, via per-open state. Exclusive access is sometimes necessary for hardware that can only support one session at a time, but it is a choice, not a default. Chapter 7 forced exclusivity as a teaching move; Chapter 8 stage 2 lifts that restriction precisely because it is not the common case.

**Q: Should I return `ENXIO` or `EBUSY` on a failed open?**

`ENXIO` when the driver is not ready. `EBUSY` when the device can be opened in principle but not right now. The user-visible messages are different, and an operator who reads your kernel log will thank you for picking the correct one.

**Q: Should I `strdup` strings I get from userland?**

Not on the open path. If a handler has a legitimate reason to remember a user-supplied string beyond the call, use `malloc(9)` with an explicit size and copy the string in. Never rely on a pointer into userland memory after a handler returns; it may no longer be valid, and even if it is, the kernel should never trust userland-owned memory for long.

**Q: Should the softc remember which descriptors have it open?**

Usually no. Per-open state via `devfs_set_cdevpriv` is the answer. If you need an iteration mechanism, `devfs_foreach_cdevpriv` exists and is correct. Do not maintain your own list of descriptor pointers in the softc; the locking is nontrivial and the kernel already provides the right answer.

**Q: When should my detach refuse with `EBUSY`?**

When the driver cannot safely tear itself down with the current state. Open descriptors are the most common reason. Some drivers also refuse if hardware is actively transferring, or if a control operation is in progress. Error out early and cleanly; do not try to coerce the system into a clean state from inside `detach`.

**Q: Can I unload the driver while descriptors are open?**

Not if `detach` refuses. If your `detach` accepts the situation, the kernel will still drain in-flight handlers, but open descriptors remain on existing file tables until the processes close them, and those descriptors will then return `ENXIO` (or similar) from further operations. For a teaching driver, refusing `EBUSY` is the cleaner choice.

These questions are the ones that will come up in your first real driver review. Having seen them once here means you are not seeing them for the first time when the reviewer asks.

### A Decision Tree for Common Design Choices

When you sit down to design a new node or change an existing one, the questions tend to fall into a small set of branches. The tree below is a field guide, not an algorithm; real design always involves judgement, but knowing the shape of the tree helps.

**Start: I want to expose something through `/dev`.**

**Branch 1: What kind of state does the node carry?**

- **Session-less, trivial data source or sink** (like `/dev/null`, `/dev/zero`): no softc, no per-open state, one `cdevsw` per behaviour. Use `make_dev` or `make_dev_s` in a `MOD_LOAD` handler. Mode typically `0666`.
- **Per-device hardware** (like a serial port, a sensor, an LED): one softc per instance, one cdev per instance. Use `attach`/`detach` pattern. Mode typically `0600` or `0660`.
- **Subsystem control** (like `/dev/pf` or `/dev/mdctl`): one cdev exposing `d_ioctl`-only operations. Mode `0600`.
- **Per-session state** (like BPF, like FUSE): one cdev per session or one cloning entry point. Per-open state via `devfs_set_cdevpriv`. Mode `0600`.

**Branch 2: How do users discover the node?**

- **Stable fixed name** (like `/dev/null`): put the name in the `make_dev` format string and leave it.
- **Per-instance numbered name** (like `/dev/myfirst0`): use `%d` in the format string and `device_get_unit(9)` for the number.
- **Subdirectory grouping** (like `/dev/led/foo`): use `/` inside the format string; devfs creates the directory on demand.
- **On-demand per-open instance**: use cloning. Covered later.

**Branch 3: Who can touch it?**

- **Anyone**: `UID_ROOT`, `GID_WHEEL`, mode `0666`. Rare; use only for harmless nodes.
- **Root only**: `UID_ROOT`, `GID_WHEEL`, mode `0600`. The default for anything privileged.
- **Root plus an operator group**: `UID_ROOT`, `GID_OPERATOR`, mode `0660`. Common for hands-on privileged tools.
- **Root write, anyone read**: `UID_ROOT`, `GID_WHEEL`, mode `0644`. For status nodes.
- **Custom named group**: define the group in `/etc/group`, use `devfs.conf` to adjust ownership at node creation time. Do not invent a group inside your driver.

**Branch 4: How many concurrent openers?**

- **Exactly one at a time**: exclusive open pattern, flag in softc, check under mutex in `d_open`, return `EBUSY` on conflict. No `devfs_set_cdevpriv`.
- **Multiple, each with independent state**: remove the exclusive check, allocate per-open struct in `d_open`, call `devfs_set_cdevpriv`, read back with `devfs_get_cdevpriv`.
- **Multiple, all sharing driver-wide state**: allocate nothing per open; just read and write the softc under its mutex.

**Branch 5: What happens when the driver unloads with users active?**

- **Refuse the unload** with `EBUSY` from `detach` as long as any descriptor is open. This is the clean default.
- **Accept the unload** but invalidate open descriptors. In this case you need a `d_purge` handler to wake any blocked threads and convince them to return promptly. More complex; do this only when the refusal would leave the system in a worse state.

**Branch 6: What kind of name adjustments do users and operators need?**

- **A second name maintained by the driver itself** (legacy path, well-known shortcut): `make_dev_alias(9)` in `attach`, `destroy_dev(9)` on it in `detach`.
- **A second name maintained by the operator**: `link` in `/etc/devfs.conf`. Driver does nothing.
- **A permission widening or narrowing per host**: `own` and `perm` in `/etc/devfs.conf`. Driver keeps its baseline.
- **A jail-filtered view**: a ruleset in `/etc/devfs.rules`, referenced in `jail.conf`. Driver has nothing to say.

**Branch 7: How do userland programs receive events from the driver?**

- **Polling by reading**: drivers that only need to hand out bytes. `d_read` and `d_write`.
- **Blocking reads with signals**: drivers that should unblock on SIGINT. Covered in Chapter 10.
- **Poll/select**: `d_poll`. Covered in Chapter 10.
- **Kqueue**: `d_kqfilter`. Covered in Chapter 10.
- **devd notifications**: `devctl_notify(9)` from the driver; operator-side rules in `/etc/devd.conf`.
- **sysctl pulls**: for observability without file-descriptor cost. Always complementary to the `/dev` surface.

This tree does not cover every case. It covers enough that a driver author can navigate the first several design decisions without panicking. When a new question comes up that is not on the tree, write down the question and the answer you settled on; that is how the tree grows for you personally.

### A Warning About Over-Engineering

A few design temptations are worth calling out specifically, because they tend to turn simple drivers into complicated drivers with no gain.

- **Inventing your own IPC protocol over `read`/`write`**. If the messages are structured, use `ioctl(2)` (Chapter 25).
- **Embedding a tiny language into `ioctl` commands** so users can "script" the driver. This is almost always a sign that the feature belongs in userland.
- **Multiplexing many unrelated subsystems through one `cdevsw`**. If two surfaces have different semantics, give them two `cdevsw` values; it costs nothing and reads better.
- **Adding `D_NEEDGIANT` to silence an SMP warning**. The warning is correct; fix the locking.
- **Handling every possible `errno` value from every possible userland program**. Pick the right one for your situation and stick with it. The standard `err(3)` family does the rest.

The discipline of "as simple as it can be, but no simpler" is especially important at this level. Every line of driver code is a line that could have a bug in it under load. A lean driver is easier to review, easier to debug, easier to port, and easier to hand off to the next maintainer.



## Hands-On Labs

These labs extend the Chapter 7 driver in place. You should not need to retype anything from scratch. The companion directory mirrors the stages.

### Lab 8.1: Structured Name and Tighter Permissions

**Goal.** Move the device from `/dev/myfirst0` to `/dev/myfirst/0`, and change the group to `operator` with mode `0660`.

**Steps.**

1. In `myfirst_attach()`, change the `make_dev_s()` format string to `"myfirst/%d"`.
2. Change `args.mda_gid` from `GID_WHEEL` to `GID_OPERATOR`, and `args.mda_mode` from `0600` to `0660`.
3. Rebuild and reload:

   ```sh
   % make clean && make
   % sudo kldload ./myfirst.ko
   % ls -l /dev/myfirst
   total 0
   crw-rw----  1 root  operator  0x5a Apr 17 09:41 0
   ```

4. Confirm that a normal user in the `operator` group can now read from the node without `sudo`. On FreeBSD, you add a user to that group with `pw groupmod operator -m yourname`, then start a fresh shell.
5. Unload the driver and confirm the `/dev/myfirst/` directory disappears along with the node.

**Success criteria.**

- `/dev/myfirst/0` appears on load and disappears on unload.
- `ls -l /dev/myfirst/0` shows `crw-rw----  root  operator`.
- A member of the `operator` group can `cat </dev/myfirst/0` without error.

### Lab 8.2: Add an Alias

**Goal.** Expose `/dev/myfirst` as an alias for `/dev/myfirst/0`.

**Steps.**

1. Add a `struct cdev *cdev_alias` field to the softc.
2. After the successful `make_dev_s()` call in `myfirst_attach()`, call:

   ```c
   sc->cdev_alias = make_dev_alias(sc->cdev, "myfirst");
   if (sc->cdev_alias == NULL)
           device_printf(dev, "failed to create alias\n");
   ```

3. In `myfirst_detach()`, destroy the alias before destroying the primary cdev:

   ```c
   if (sc->cdev_alias != NULL) {
           destroy_dev(sc->cdev_alias);
           sc->cdev_alias = NULL;
   }
   if (sc->cdev != NULL) {
           destroy_dev(sc->cdev);
           sc->cdev = NULL;
   }
   ```

4. Rebuild, reload, and verify:

   ```sh
   % ls -l /dev/myfirst /dev/myfirst/0
   ```

   Both paths should respond. `sudo cat </dev/myfirst` and `sudo cat </dev/myfirst/0` should behave identically.

**Success criteria.**

- Both paths exist while the driver is loaded.
- Both paths disappear on unload.
- The driver does not panic or leak if the alias creation fails; comment out the `make_dev_alias` line temporarily to confirm this.

### Lab 8.3: Per-Open State

**Goal.** Give each `open(2)` its own small structure, and verify from userland that two descriptors see independent data.

**Steps.**

1. Add the `struct myfirst_fh` type and the `myfirst_fh_dtor()` destructor as shown earlier in this chapter.
2. Rewrite `myfirst_open()` to allocate a `myfirst_fh`, call `devfs_set_cdevpriv()`, and free on registration failure. Remove the exclusive-open check.
3. Rewrite `myfirst_read()` and `myfirst_write()` so each starts with a call to `devfs_get_cdevpriv(&fh)`. Leave the body unchanged for now; Chapter 9 fills it in.
4. Rebuild, reload, then run two `probe_myfirst` processes side by side:

   ```sh
   % (sudo ./probe_myfirst &) ; sudo ./probe_myfirst
   ```

5. In `dmesg`, confirm that the two `open (per-open fh=...)` messages show different pointers.

**Success criteria.**

- Two simultaneous opens succeed. No `EBUSY`.
- Two distinct `fh=` pointers appear in the kernel log.
- `kldunload myfirst` is only possible once both probes have exited.

### Lab 8.4: devfs.conf Persistence

**Goal.** Make the ownership change from Lab 8.1 survive reboots, without editing the driver again.

**Steps.**

1. In Lab 8.1, revert `args.mda_gid` and `args.mda_mode` to the Chapter 7 defaults (`GID_WHEEL`, `0600`).
2. Create or edit `/etc/devfs.conf` and add:

   ```
   own     myfirst/0       root:operator
   perm    myfirst/0       0660
   ```

3. Apply the change without rebooting:

   ```sh
   % sudo service devfs restart
   ```

4. Reload the driver and confirm that `ls -l /dev/myfirst/0` again shows `root  operator  0660`, even though the driver itself asked for `root  wheel  0600`.

**Success criteria.**

- With the driver loaded and `devfs.conf` in place, the node shows the `devfs.conf` values.
- With the driver loaded and the `devfs.conf` lines commented out and devfs restarted, the node returns to the driver's baseline.

**Notes.** Lab 8.4 is an operator-side lab. The driver does not change between steps. The point is to see the two-layer policy model at work: driver sets the baseline, `devfs.conf` shapes the view.

### Lab 8.5: Two-Node Driver (Data and Control)

**Goal.** Extend `myfirst` to expose two distinct nodes: a data node at `/dev/myfirst/0` and a control node at `/dev/myfirst/0.ctl`, each with its own `cdevsw` and its own permission mode.

**Prerequisites.** Completed Lab 8.3 (stage 2 with per-open state).

**Steps.**

1. Define a second `struct cdevsw` in the driver, named `myfirst_ctl_cdevsw`, with `d_name = "myfirst_ctl"` and only `d_ioctl` stubbed (you will not implement ioctl commands; just make the function exist and return `ENOTTY`).
2. Add a `struct cdev *cdev_ctl` field to the softc.
3. In `myfirst_attach`, after the data node is created, create the control node with a second `make_dev_s` call. Use `"myfirst/%d.ctl"` as the format. Set the mode to `0640` and the group to `GID_WHEEL` so the control node is narrower than the data node.
4. Pass `sc` through `mda_si_drv1` for the control cdev as well, so `d_ioctl` can find it.
5. In `myfirst_detach`, destroy the control cdev **before** the data cdev. Log each destruction.
6. Rebuild, reload, and verify:

   ```sh
   % ls -l /dev/myfirst
   total 0
   crw-rw----  1 root  operator  0x5a Apr 17 10:02 0
   crw-r-----  1 root  wheel     0x5b Apr 17 10:02 0.ctl
   ```

**Success criteria.**

- Both nodes appear on load.
- Both nodes disappear on unload.
- The data node is group-readable by `operator`; the control node is not.
- Attempting `cat </dev/myfirst/0.ctl` from a non-root non-wheel user fails with `Permission denied`.

**Notes.** In real drivers the control node is where `ioctl` commands for configuration live. This chapter does not implement any `ioctl` commands; that work is Chapter 25's. The point of Lab 8.5 is to show that you can have two nodes with different policies wired into one driver.

### Lab 8.6: Parallel Probe Verification

**Goal.** Use the `parallel_probe` tool from the companion tree to prove that per-open state really is per-descriptor.

**Prerequisites.** Completed Lab 8.3. The stage 2 driver is loaded.

**Steps.**

1. Build the userland tools:

   ```sh
   % cd examples/part-02/ch08-working-with-device-files/userland
   % make
   ```

2. Run `parallel_probe` with four descriptors:

   ```sh
   % sudo ./parallel_probe /dev/myfirst/0 4
   opened /dev/myfirst/0 as fd 3
   opened /dev/myfirst/0 as fd 4
   opened /dev/myfirst/0 as fd 5
   opened /dev/myfirst/0 as fd 6
   holding 4 descriptors; press enter to close
   ```

3. Open a second terminal and inspect `dmesg`:

   ```sh
   % dmesg | tail -20
   ```

   You should see four `open via myfirst/0 fh=<pointer> (active=N)` lines, each with a different pointer value.

4. In the second terminal, check the active-opens sysctl:

   ```sh
   % sysctl dev.myfirst.0.stats.active_fhs
   dev.myfirst.0.stats.active_fhs: 4
   ```

5. Return to the first terminal and press Enter. The probe closes all four descriptors. Check `dmesg` again:

   ```sh
   % dmesg | tail -10
   ```

   You should see four `per-open dtor fh=<pointer>` lines, one per descriptor, with the same pointer values that appeared in the open log.

6. Verify `active_fhs` is back to zero:

   ```sh
   % sysctl dev.myfirst.0.stats.active_fhs
   dev.myfirst.0.stats.active_fhs: 0
   ```

**Success criteria.**

- Four distinct `fh=` pointers in the open log.
- Four matching pointers in the destructor log.
- `active_fhs` increments to four and decrements back to zero.
- No kernel log messages about leaked memory or unexpected state.

**Notes.** Lab 8.6 is the strongest evidence you can easily produce that per-open state is isolated. If any step fails, the most common culprit is a missed call to `devfs_set_cdevpriv` or a destructor that does not decrement `active_fhs`.

### Lab 8.7: devfs.rules for a Jail

**Goal.** Make `/dev/myfirst/0` visible inside a jail through a devfs ruleset.

**Prerequisites.** A working FreeBSD jail on your lab system. If you do not have one yet, skip this lab and return after Part 7.

**Steps.**

1. Add a ruleset to `/etc/devfs.rules`:

   ```
   [myfirst_jail=100]
   add include $devfsrules_jail
   add path 'myfirst'   unhide
   add path 'myfirst/*' unhide
   add path 'myfirst/*' mode 0660 group operator
   ```

2. Add a devfs entry to the jail's `jail.conf`:

   ```
   myfirstjail {
           path = "/jails/myfirstjail";
           host.hostname = "myfirstjail.example.com";
           mount.devfs;
           devfs_ruleset = 100;
           exec.start = "/bin/sh";
           persist;
   }
   ```

3. Reload devfs and start the jail:

   ```sh
   % sudo service devfs restart
   % sudo service jail start myfirstjail
   ```

4. Inside the jail, confirm the node:

   ```sh
   % sudo jexec myfirstjail ls -l /dev/myfirst
   ```

5. Verify the ruleset is working by commenting out the `add path 'myfirst/*' unhide` line, restarting devfs and the jail, and observing the node disappear.

**Success criteria.**

- `/dev/myfirst/0` appears inside the jail with mode `0660` and group `operator`.
- Removing the unhide rule removes the node from inside the jail.
- The host continues to see the node regardless of the jail's ruleset.

**Notes.** Jail configuration is ordinarily covered in later chapters; this lab is a preview to demonstrate the driver-side outcome. If the lab is difficult on your system, come back to it after you have jails configured for other purposes.

### Lab 8.8: Destroy-Dev Drain

**Goal.** Demonstrate the difference between `destroy_dev` and `destroy_dev_drain` when a `cdevsw` is being freed along with many cdevs.

**Prerequisites.** Completed Lab 8.3. Your driver is loaded and quiet.

**Steps.**

1. Review the Stage 2 detach code. The one-cdev driver does not need `destroy_dev_drain`. The lab models what goes wrong in a multi-cdev driver that does.
2. Build the `stage4-destroy-drain` variant of the driver (in the companion tree). This variant creates five cdevs in attach and uses `destroy_dev_sched` to schedule their destruction in detach, without draining.
3. Load the variant, then immediately unload it while a userland process is holding one of the cdevs open:

   ```sh
   % sudo kldload ./stage4.ko
   % sudo ./hold_myfirst 60 /dev/myfirstN/3 &
   % sudo kldunload stage4
   ```

4. Observe the kernel log. You should see complaints or, depending on timing, a panic. The variant is deliberately unsafe.
5. Switch to the "fixed" version of the stage 4 source, which calls `destroy_dev_drain(&mycdevsw)` after the per-cdev destroy-sched calls. Repeat the load/hold/unload sequence.
6. Confirm that the fixed version unloads cleanly, waiting for the held descriptor to close before the module goes away.

**Success criteria.**

- The broken variant produces an observable problem (message, hang, or panic) when unloaded with a descriptor held.
- The fixed variant completes the unload cleanly.
- Reading the source makes it clear which call made the difference.

**Notes.** This lab deliberately triggers a bad state. Run it in a throwaway VM, not on a system you care about. The point is to build intuition for why `destroy_dev_drain` exists; once you have watched the broken path fail, you will remember to call it in multi-cdev drivers.



## Challenge Exercises

These build on the labs. Take your time; none of them introduce new mechanics, they only stretch the ones you just practised.

### Challenge 1: Use the Alias

Change `probe_myfirst.c` to open `/dev/myfirst` instead of `/dev/myfirst/0` by default. Confirm from the kernel log that your `d_open` runs, and that `devfs_set_cdevpriv` succeeds exactly once per `open(2)`. Then change the path back. You should not have to edit the driver.

### Challenge 2: Observe Per-Open Cleanup

Add a `device_printf` inside `myfirst_fh_dtor()` that logs the `fh` pointer being freed. Run `probe_myfirst` once and confirm that exactly one destructor line appears in `dmesg` per run. Then write a tiny program that opens the device, sleeps for 30 seconds, and exits without calling `close(2)`. Confirm that the destructor still fires when the process exits. Cleanup is not a politeness; it is guaranteed.

### Challenge 3: Experiment with devfs.rules

If you have a FreeBSD jail configured, add a `myfirst_lab` ruleset to `/etc/devfs.rules` that makes `/dev/myfirst/*` visible inside the jail. Start the jail, open the device from inside it, and confirm that the driver sees a new open. If you do not have a jail yet, skip this challenge for now and return to it after Part 7.

### Challenge 4: Read Two More Drivers

Pick two drivers from `/usr/src/sys/dev/` that you have not read yet. Good candidates are `/usr/src/sys/dev/random/randomdev.c`, `/usr/src/sys/dev/hwpmc/hwpmc_mod.c`, `/usr/src/sys/dev/kbd/kbd.c`, or anything else short enough to skim. For each driver, find:

- The `cdevsw` definition and its `d_name`.
- The `make_dev*` call and the permission mode it sets.
- The `destroy_dev` calls, or absence of them.
- Whether the driver uses `devfs_set_cdevpriv`.
- Whether the driver creates a subdirectory under `/dev`.

Write a short paragraph for each driver classifying its device-file surface. The point is to sharpen your eye; there is no single correct taxonomy.

### Challenge 5: devd Configuration

Write a minimal `/etc/devd.conf` rule that logs a message every time `/dev/myfirst/0` appears or disappears. The devd configuration format is documented in `devd.conf(5)`. A starting template:

```text
notify 100 {
        match "system"      "DEVFS";
        match "subsystem"   "CDEV";
        match "cdev"        "myfirst/0";
        action              "/usr/bin/logger -t myfirst event=$type";
};
```

Install the rule, restart devd (`service devd restart`), load and unload the driver, then verify that `grep myfirst /var/log/messages` shows both events.

### Challenge 6: Add a Status Node

Modify `myfirst` to expose a read-only status node alongside the data node. The status node lives at `/dev/myfirst/0.status`, mode `0444`, owner `root:wheel`. Its `d_read` returns a short plaintext string summarising the current state of the driver:

```ini
attached_at=12345
active_fhs=2
open_count=17
```

Hint: allocate a small fixed-size buffer in the softc, format the string under the mutex, and return it to the user with `uiomove(9)` if you have read Chapter 9, or with a manual implementation for now.

If you are not yet comfortable with `uiomove`, defer this challenge until after Chapter 9. It is a natural first use of what Chapter 9 teaches.



## Error Codes for Device File Operations

Every `d_open` and `d_close` that returns a non-zero value tells devfs something specific. The errno values you choose are the contract between your driver and every user program that ever touches your node. Getting them right costs nothing; getting them wrong produces bug reports you will not understand at first read.

This section surveys the errno values that come up in practice on the device-file surface. Chapter 9 will treat the errno choices for `d_read` and `d_write` separately, because the data-path choices are different in character. Here we stay focused on open, close, and ioctl-adjacent returns.

### The Short List

In rough order of how often you will reach for them:

- **`ENXIO` (No such device or address)**: "The device is not in a state where it can be opened." Use when the driver is attached but not ready, when the hardware is known to be missing, when the softc is in a transient state. The user sees `Device not configured`.
- **`EBUSY` (Device busy)**: "The device is already open and this driver does not allow concurrent access." Use for exclusive-open policies. The user sees `Device busy`.
- **`EACCES` (Permission denied)**: "The credential presenting this open is not allowed." The kernel normally catches permission failures before your handler runs, but a driver may check a secondary policy (for instance, an `ioctl`-only node that refuses opens for read) and return `EACCES` itself.
- **`EPERM` (Operation not permitted)**: "The operation requires privilege the caller does not have." Similar to `EACCES` in spirit but aimed at privilege distinctions (`priv_check(9)` failures) rather than UNIX file permissions.
- **`EINVAL` (Invalid argument)**: "The call was structurally valid but the driver does not accept these arguments." Use when `oflags` specifies a combination the driver refuses.
- **`EAGAIN` (Resource temporarily unavailable)**: "The device could be opened in principle, but not right now." Use this when you have a temporary shortage (a slot is full, a resource is being reconfigured) and the user should retry later. The user sees `Resource temporarily unavailable`.
- **`EINTR` (Interrupted system call)**: Returned when a sleep inside your handler is interrupted by a signal. You will not normally return this from `d_open` because opens do not usually sleep interruptibly. It shows up more in data-path handlers.
- **`ENOENT` (No such file or directory)**: Almost always synthesised by devfs itself when the path does not resolve. A driver rarely returns this from its own handlers.
- **`ENODEV` (Operation not supported by device)**: "The operation itself is valid but this device does not support it." Use when a secondary interface of the driver refuses an operation the other interface supports.
- **`EOPNOTSUPP` (Operation not supported)**: A cousin of `ENODEV`. Used in some subsystems for similar situations.

### Which Value for Which Situation?

Real drivers fall into patterns. Here are the patterns you will write most often.

**Pattern A: Driver attached but softc not yet ready.** You might hit this during a two-stage attach where the cdev is created before some initialisation completes, or during detach while the cdev still exists.

```c
if (sc == NULL || !sc->is_attached)
        return (ENXIO);
```

**Pattern B: Exclusive-open policy.**

```c
mtx_lock(&sc->mtx);
if (sc->is_open) {
        mtx_unlock(&sc->mtx);
        return (EBUSY);
}
sc->is_open = 1;
mtx_unlock(&sc->mtx);
```

This is what Chapter 7 did. Chapter 8's stage 2 removes the exclusive check because per-open state is available; the `EBUSY` is simply no longer needed.

**Pattern C: Read-only node refusing writes.**

```c
if ((oflags & FWRITE) != 0)
        return (EACCES);
```

Use this when the node is conceptually read-only and opening for write is a caller mistake.

**Pattern D: Privileged-only interface.**

```c
if (priv_check(td, PRIV_DRIVER) != 0)
        return (EPERM);
```

Returns `EPERM` when a non-privileged caller tries to open a node that enforces additional privilege checks beyond the filesystem mode.

**Pattern E: Temporarily unavailable.**

```c
if (sc->resource_in_flight) {
        return (EAGAIN);
}
```

Use this when the driver can accept the open later but not now, and the user should retry.

**Pattern F: Driver-specific invalid combination.**

```c
if ((oflags & O_NONBLOCK) != 0 && !sc->supports_nonblock) {
        return (EINVAL);
}
```

Use this when the caller's `oflags` specify a mode your driver does not implement.

### Returning Errors From d_close

`d_close` has its own considerations. The kernel does not usually care about errors from close, because by the time `close(2)` returns to userland the descriptor is already gone. But close is still your last chance to notice a failure and log it, and some callers may check. The safest pattern is:

- Return zero from ordinary close paths.
- Return a non-zero errno only when something genuinely unusual happened and userland should know about it.
- When in doubt, log with `device_printf(9)` and return zero.

A driver that returns random errors from `d_close` is a driver whose tests will mysteriously fail, because most userland code ignores close errors. Save errno for open and for ioctl, where it matters.

### Mapping Your errno to User Messages

The values defined in `/usr/include/errno.h` have stable textual representations through `strerror(3)` and `perror(3)`. Every `err(3)` and `warn(3)` message in a userland program will use these. A short table of the mappings:

| errno             | `strerror` text                   | Typical user program behavior |
|-------------------|-----------------------------------|-------------------------------|
| `ENXIO`           | Device not configured             | Wait or give up; report clearly |
| `EBUSY`           | Device busy                       | Retry later or abort            |
| `EACCES`          | Permission denied                 | Prompt for `sudo` or exit       |
| `EPERM`           | Operation not permitted           | Similar to `EACCES`             |
| `EINVAL`          | Invalid argument                  | Report bug in calling code      |
| `EAGAIN`          | Resource temporarily unavailable  | Retry after a short delay       |
| `EINTR`           | Interrupted system call           | Retry, usually in a loop        |
| `ENOENT`          | No such file or directory         | Verify driver is loaded         |
| `ENODEV`          | Operation not supported by device | Report design mismatch          |
| `EOPNOTSUPP`      | Operation not supported           | Report design mismatch          |

Appendix E of this book collects the full list of kernel errno values and their meanings. For Chapter 8 the list above covers everything you will reach for on the device-file surface.

### A Quick Checklist Before You Pick an errno

When you are unsure which errno fits, ask three questions:

1. **Is the problem about identity?** "This device cannot be opened now" is `ENXIO`. "This device does not exist" is `ENOENT`. Rarely the driver's call; devfs usually handles it.
2. **Is the problem about permission?** "You do not have permission" is `EACCES`. "You lack a specific privilege" is `EPERM`.
3. **Is the problem about arguments?** "The call was structurally fine but the driver will not accept these arguments" is `EINVAL`.

When two errno values could plausibly fit, pick the one whose textual representation matches what you would want a frustrated user to read. Remember that errno values become error messages in tools you do not control, and the clearer the mapping between kernel intent and user-facing text, the more kindly your driver will be reviewed.

### A Short Narrative: errno Chosen Three Times

To make the abstract concrete, here are three small scenes drawn from real driver review conversations. Each is about the choice of a single errno value.

**Scene 1. The too-early open.**

A driver attaches an onboard sensor. The sensor takes a hundred milliseconds after power-on to produce valid data. During those hundred milliseconds, user programs that try to read will get garbage.

The first draft of the driver returns `EAGAIN` from `d_open` during the warm-up window. The reviewer flags it. `EAGAIN` means "retry later", which is fine, but the user-facing text is "Resource temporarily unavailable", and that does not match what the user is seeing: the device exists and can in principle be opened, but it is not producing data yet.

The revised draft returns `ENXIO` during the warm-up. The user sees "Device not configured", which is closer to the truth. A well-written userland program can special-case that errno if it wants to wait for the device. A typical tool will print a clear message and exit.

Lesson: think about what the user sees, not just what you mean internally.

**Scene 2. The wrong permission error.**

A driver has a configurable mode: a sysctl can set it to "read-only". When the sysctl is set, `d_write` returns an error. The first draft returns `EPERM`. The reviewer flags it. `EPERM` is about privilege; the kernel uses it when a specific `priv_check(9)` call fails. But in this driver, no privilege check is being performed; the device is simply in a read-only state.

The revised draft returns `EROFS`, "Read-only file system". The textual mapping is almost perfect for this scenario.

Lesson: the nearer errno value is usually the better errno value. Do not default to `EPERM` for every refusal.

**Scene 3. The busy file.**

A driver that enforces exclusive access returns `EBUSY` from `d_open` when a second opener arrives. That is correct. In code review, one reviewer points out that the driver also returns `EBUSY` from a control-node ioctl that refuses during an in-progress reconfiguration. The review argument is that those are different situations and the two uses of `EBUSY` will confuse operators who are reading logs.

The discussion lands on a compromise: `EBUSY` for the open-path exclusive check, `EAGAIN` for the reconfiguration-in-progress case. The distinction is that the open-path refusal is "will be busy until the other user closes", while the reconfiguration refusal is "retry in a moment, it will clear on its own".

Lesson: two situations that feel similar may map to different errno values if the reasoning about the user's next action differs.

These scenes are small, but the principle is not. Every errno value is a hint to the user about what to do next. Choose it with the user's perspective in view, not only yours.

### Using `err(3)` and `warn(3)` to Exercise errno Values

The `err(3)` family in FreeBSD's libc prints a clean "program: message: errno-string" when an operation fails. Your userland probes use `err(3)` because it is the shortest path to a readable error. You can verify your driver's errno choices by running a probe that deliberately triggers each one:

```c
fd = open("/dev/myfirst/0", O_RDWR);
if (fd < 0)
        err(1, "open /dev/myfirst/0");
```

When the driver returns `EBUSY`, the program prints:

```text
probe_myfirst: open /dev/myfirst/0: Device busy
```

When the driver returns `ENXIO`, the program prints:

```text
probe_myfirst: open /dev/myfirst/0: Device not configured
```

Run the probe against each of the error cases you can construct. Read the messages out loud. If any of them would confuse a user who had not read your driver's source code, reconsider the errno.

### errno Values Your Driver Should Almost Never Return

For balance, a list of values that rarely fit a device-file open or close:

- **`ENOMEM`**: let the `malloc` call report this by returning it through your function, but do not invent it.
- **`EIO`**: reserved for hardware I/O errors. If your device has no hardware, this value is out of place.
- **`EFAULT`**: used when userland hands the kernel a bad pointer. On the open path you rarely touch user pointers, so `EFAULT` does not fit.
- **`ESRCH`**: "No such process". Unlikely to be right for a device-file operation.
- **`ECHILD`**: process-relationship errno. Not applicable.
- **`EDOM`** and **`ERANGE`**: math errors. Not applicable.

When in doubt, if the value does not appear in the Chapter 8 "Short List" earlier in this section, it is almost certainly wrong for an open or close. Save the unusual values for the unusual operations that genuinely produce them.



## Tools for Inspecting /dev

Several small utilities are worth knowing about, because once you reach Chapter 9 you will be leaning on them to confirm behavior quickly. This section introduces each in enough depth to use it, and ends with two short troubleshooting walkthroughs.

### ls -l for Permissions and Existence

The first stop. `ls -l /dev/yourpath` confirms existence, type, ownership, and mode. If the node is missing after a load, your `make_dev_s` likely failed; check `dmesg` for the error code.

`ls -l` on a devfs directory works the way you expect: `ls -l /dev/myfirst` lists the entries in the subdirectory. Combined with `-d`, it reports on the directory itself:

```sh
% ls -ld /dev/myfirst
dr-xr-xr-x  2 root  wheel  512 Apr 17 10:02 /dev/myfirst
```

The mode on a devfs subdirectory is `0555` by default, and it is not directly configurable through `devfs.conf`. The subdirectory exists only because at least one node is inside it; when the last node inside disappears, the directory disappears too.

### stat and stat(1)

`stat(1)` prints a structured view of any node. The default output is verbose and includes timestamps. A more useful form is a custom format:

```sh
% stat -f '%Sp %Su %Sg %T %N' /dev/myfirst/0
crw-rw---- root operator Character Device /dev/myfirst/0
```

The placeholders are documented in `stat(1)`. The five above are permissions, user name, group name, file type description, and path. This form is useful inside scripts that need a stable text representation.

For comparing two paths to check that they resolve to the same cdev, `stat -f '%d %i %Hr,%Lr'` prints the device of the filesystem, the inode, and the major and minor components of `rdev`. On two devfs nodes that refer to the same cdev, the `rdev` component will match.

### fstat(1): Who Has It Open?

`fstat(1)` lists every open file on the system. Filtered to a device path, it tells you which processes have the node open:

```sh
% fstat /dev/myfirst/0
USER     CMD          PID   FD MOUNT      INUM MODE         SZ|DV R/W NAME
root     probe_myfir  1234    3 /dev          4 crw-rw----   0,90 rw  /dev/myfirst/0
```

This is the tool that solves the "`kldunload` returns `EBUSY` and I do not know why" puzzle. Run it against your node, identify the offending process, and either wait for it to finish or terminate it.

`fstat -u username` filters by user, useful when you suspect a particular user's daemons are holding the node. `fstat -p pid` inspects one process.

### procstat -f: A Process-First View

`fstat(1)` lists files and tells you who holds them. `procstat -f pid` does the inverse: it lists the files held by a given process. When you have the PID of a running program and want to confirm which device nodes it currently has open, this is the tool:

```sh
% procstat -f 1234
  PID COMM                FD T V FLAGS    REF  OFFSET PRO NAME
 1234 probe_myfirst        3 v c rw------   1       0     /dev/myfirst/0
```

Column `T` shows the file type (`v` for vnode, which includes device files), and column `V` shows the vnode type (`c` for character-device vnode). This is the quickest way to confirm what a debugger shows you.

### devinfo(8): The Newbus Side

`devinfo(8)` does not look at devfs at all. It walks the Newbus device tree and prints the device hierarchy. Your `myfirst0` child of `nexus0` shows up there regardless of whether a cdev exists:

```sh
% devinfo -v
nexus0
  myfirst0
  pcib0
    pci0
      <...lots of PCI children...>
```

This is the tool you reach for when something is missing from `/dev` and you need to check whether the device itself attached. If `devinfo` shows `myfirst0` but `ls /dev` does not, your `make_dev_s` failed. If neither shows the device, your `device_identify` or `device_probe` did not create the child. Two different bugs, two different fixes.

The `-r` flag filters to the Newbus hierarchy rooted at a specific device, which becomes useful in complex systems with lots of PCI devices.

### devfs(8): Rulesets and Rules

`devfs(8)` is the low-level administrative interface to devfs rulesets. You met it in Section 10. Three subcommands come up often:

- `devfs rule showsets` lists ruleset numbers currently loaded.
- `devfs rule -s N show` prints the rules inside ruleset `N`.
- `devfs rule -s N add path 'pattern' action args` adds a rule at runtime.

Rules added at runtime do not persist; to make them permanent, add them to `/etc/devfs.rules` and run `service devfs restart`.

### sysctl dev.* and Other Hierarchies

`sysctl dev.myfirst` prints every sysctl variable under your driver's namespace. From Chapter 7 you already have a `dev.myfirst.0.stats` tree. Reading it confirms the softc is present, the attach ran, and the counters are advancing.

Sysctls are a complementary surface to `/dev`. They are primarily for observability; they are cheaper to read than opening a device; they have no file-descriptor cost. When a piece of information is simple enough to be a number or a short string, consider exposing it as a sysctl rather than as a read on the device node.

### kldstat: Is the Module Loaded?

When a node is missing, the question "is my driver even loaded?" is worth asking first.

```sh
% kldstat | grep myfirst
 8    1 0xffffffff82a00000     3a50 myfirst.ko
```

If you see the module in `kldstat`, the module is in the kernel. If `devinfo` shows the device but `ls /dev` does not show the node, the issue is inside your driver. If `kldstat` does not show the module, the issue is outside: you forgot to `kldload`, or the load failed. Check `dmesg`.

### dmesg: The Log of What Happened

Every `device_printf` and `printf` call from a driver ends up in the kernel message buffer, and `dmesg` (or `dmesg -a`) prints it. When something goes wrong on this surface, `dmesg` is the first place to look:

```sh
% dmesg | tail -20
```

Your attach and detach messages, any `make_dev_s` failures, and any panic messages from destruction paths land here. Get in the habit of watching `dmesg` with a second terminal open to `tail -f /var/log/messages` during development.

### Troubleshooting Walkthrough 1: The Node Is Missing

A checklist for "I expected `/dev/myfirst/0` to exist and it does not".

1. Is the module loaded? `kldstat | grep myfirst`.
2. Did attach run? `devinfo -v | grep myfirst`.
3. Did `make_dev_s` succeed? `dmesg | tail` should show your attach-success message.
4. Is devfs mounted on `/dev`? `mount | grep devfs`.
5. Are you looking at the right path? If your format string was `"myfirst%d"`, the node is `/dev/myfirst0`, not `/dev/myfirst/0`. Typos happen.
6. Is a `devfs.rules` entry hiding the node? `devfs rule showsets` and inspect.

Nine times out of ten, one of the first three questions yields the answer.

### Troubleshooting Walkthrough 2: kldunload Returns EBUSY

A checklist for "I can load my module but cannot unload it".

1. Is the node still open? `fstat /dev/myfirst/0` shows the holder.
2. Is your detach returning `EBUSY` itself? Check `dmesg` for a message from your driver. Stage 2's detach returns `EBUSY` when `active_fhs > 0`.
3. Is a `devfs.conf` `link` pointing at your node? The link can keep a reference if the target is held open.
4. Is a kernel thread stuck inside one of your handlers? Look for a `Still N threads in foo` message in `dmesg`. If present, you need a `d_purge`.

Most `EBUSY`s are open descriptors. The other cases are rare.

### A Note on Habits

None of these tools are unusual. They are the everyday instruments of FreeBSD administration. What matters is the habit of reaching for them in a known order when something looks wrong. The first three times you debug a missing node, you will grope for the right tool; the fourth time, the order will feel automatic. Build that reflex now, while the problems are small.



## Pitfalls and Things Worth Watching

A field guide to the mistakes that catch beginners most often. Each one names the symptom, the cause, and the cure.

- **Creating the device node before the softc is ready.** *Symptom:* open causes a NULL dereference as soon as the driver loads. *Cause:* `si_drv1` still unset, or a softc field that `open()` consults has not been initialized. *Cure:* set `mda_si_drv1` in `make_dev_args` and finalize softc fields before the `make_dev_s` call. Think of `make_dev_s` as publishing, not preparing.
- **Destroying the softc before the device node.** *Symptom:* occasional panics during or shortly after `kldunload`. *Cause:* reversing the order of teardown in `detach()`. *Cure:* always destroy the cdev first, then the alias, then the lock, then the softc. The cdev is the door; close it before dismantling the rooms behind it.
- **Storing per-open state on the cdev.** *Symptom:* works fine with one user, garbled state with two. *Cause:* read positions or similar per-descriptor data stored in `si_drv1` or in the softc. *Cure:* move them into a `struct myfirst_fh` and register it with `devfs_set_cdevpriv`.
- **Forgetting that `/dev` changes are not persistent.** *Symptom:* a `chmod` you ran by hand is gone after a reboot or module reload. *Cause:* devfs is live, not on disk. *Cure:* put the change in `/etc/devfs.conf` and `service devfs restart`.
- **Leaking the alias on detach.** *Symptom:* `kldunload` returns `EBUSY` and the driver is wedged. *Cause:* the alias cdev is still live. *Cure:* call `destroy_dev(9)` on the alias before the primary in `detach()`.
- **Calling `devfs_set_cdevpriv` twice.** *Symptom:* the second call returns `EBUSY` and your handler returns the error to the user. *Cause:* two independent paths in `open` both tried to register private data, or the handler ran twice for the same open. *Cure:* audit the code path so exactly one successful `devfs_set_cdevpriv` happens per `d_open` invocation.
- **Allocating `fh` without freeing it on the error path.** *Symptom:* steady memory leak correlated with failed opens. *Cause:* `devfs_set_cdevpriv` returned an error and the allocation was abandoned. *Cure:* on any error after `malloc` and before a successful `devfs_set_cdevpriv`, `free` the allocation explicitly.
- **Confusing aliases with symlinks.** *Symptom:* permissions set through `devfs.conf` on a `link` do not match what the driver advertises on the primary. *Cause:* mixing both mechanisms on the same name. *Cure:* pick one tool per name; use aliases when the driver owns the name, symlinks when operator convenience is the goal.
- **Using wide-open modes for "just testing".** *Symptom:* a driver that shipped to staging with `0666` suddenly needs to have that narrowed without breaking consumers. *Cause:* a temporary lab mode turned into a default. *Cure:* default to `0600`, widen only when a concrete consumer asks, and note the reason in a comment next to the `mda_mode` line.
- **Using `make_dev` in new code.** *Symptom:* the driver compiles and works, but a reviewer flags the call. *Cause:* `make_dev` is the oldest form of the family and panics on failure. *Cure:* use `make_dev_s` with a populated `struct make_dev_args`. The newer form is easier to read, easier to error-check, and friendlier to future API additions. *How you catch it earlier:* run `mandoc -Tlint` on your driver and read the `SEE ALSO` in `make_dev(9)`.
- **Forgetting `D_VERSION`.** *Symptom:* the driver loads but the first `open` returns a cryptic failure, or the kernel prints a cdevsw version mismatch. *Cause:* the `d_version` field of `cdevsw` was left zero. *Cure:* set `.d_version = D_VERSION` as the first field in every `cdevsw` literal. *How you catch it earlier:* a code template that includes the field keeps you from ever typing a `cdevsw` without it.
- **Shipping with `D_NEEDGIANT` "because it compiled".** *Symptom:* the driver works but every operation serialises behind the Giant lock, making SMP-heavy workloads slow. *Cause:* the flag was copied from an older driver, or added to silence a warning, and never removed. *Cure:* delete the flag. If your driver actually needs Giant to hold together, it has a real locking bug that needs real fixing, not a flag.
- **Hard-coding the hex identifier in test scripts.** *Symptom:* a test fails on a slightly different machine because the `0x5a` in `ls -l` output is different there. *Cause:* devfs's `rdev` identifier is not stable across reboots, kernels, or systems. *Cure:* compare `stat -f '%d %i'` across two paths to check alias equivalence rather than scraping `ls -l` for the hex identifier.
- **Assuming `devfs.conf` runs before your driver loads.** *Symptom:* a `devfs.conf` line for your driver's node does not take effect after `kldload`. *Cause:* `service devfs start` runs early in boot, before modules loaded at runtime. *Cure:* `service devfs restart` after loading the driver, or statically compile the driver so its nodes exist before devfs starts.
- **Relying on node names with non-POSIX characters.** *Symptom:* shell scripts break with quoting errors; `devfs.rules` patterns fail to match. *Cause:* the node name uses spaces, colons, or non-ASCII characters. *Cure:* stick to lowercase ASCII letters, digits, and the three separators `/`, `-`, `.`. Other characters will sometimes work and sometimes not, and the "sometimes not" always surfaces at the worst moment.
- **Leaking per-open state on the error path of `d_open`.** *Symptom:* subtle memory leak, detected much later by running a stress test for hours. *Cause:* `malloc` succeeded, `devfs_set_cdevpriv` failed, and the allocation was abandoned without being freed. *Cure:* every error path in `d_open` between `malloc` and the successful `devfs_set_cdevpriv` must `free` the allocation. Writing the error path first, before the success path, is a useful habit.
- **Registering `devfs_set_cdevpriv` twice in the same open.** *Symptom:* the second call returns `EBUSY` and the user sees `Device busy` on open, for no reason they can discern. *Cause:* two independent code paths in `d_open` both try to attach private data, or the open handler runs twice for the same file. *Cure:* audit the code path so exactly one successful `devfs_set_cdevpriv` happens per `d_open` invocation. If the driver genuinely wants to replace the data, use `devfs_clear_cdevpriv(9)` first, but this is almost always a sign the design needs a rethink.

### Pitfalls That Are Really About Lifecycle

A separate cluster of pitfalls comes from confusion about lifecycle. They are worth calling out explicitly.

- **Freeing the softc before the cdev is destroyed.** *Symptom:* a panic shortly after `kldunload`, usually a NULL dereference or a use-after-free in a handler. *Cause:* the driver tore down softc state in `detach` before `destroy_dev` finished draining the cdev, and an in-flight handler then dereferenced the freed state. *Cure:* destroy the cdev first and rely on its draining behavior; only tear down softc after. *How you catch it earlier:* run any of the stress tests while watching `dmesg` for kernel panics; the race is easy to hit on an SMP system with moderate load.
- **Assuming `destroy_dev` returns immediately.** *Symptom:* a deadlock, usually in a handler that holds a lock and then calls a function that ends up needing the same lock. *Cause:* `destroy_dev` blocks until in-flight handlers return; if the caller holds a lock one of those handlers needs, the system deadlocks. *Cure:* never call `destroy_dev` while holding a lock an in-flight handler might need. For the common case in `detach`, hold nothing.
- **Forgetting to set `is_attached = 0` on an error unwind.** *Symptom:* subtle misbehavior after a failed load-unload-reload cycle; handlers believe the device is still attached and try to use freed state. *Cause:* a `goto fail_*` path that did not clear the flag. *Cure:* the single-label unwind pattern from Chapter 7; the last fail label always clears `is_attached` before returning.

### Pitfalls in Permission and Policy

Two categories of mistakes around permission tend to surface long after the driver ships.

- **Assuming a node is "only visible to root" because you created it with `0600`.** *Symptom:* a security review flags the node as reachable from a jail that should not see it. *Cause:* mode alone does not filter jail visibility; `devfs.rules` is the filter, and the default may be inclusive enough to pass the node to the jail. *Cure:* if the node must not be visible inside jails, ensure the default jail ruleset hides it. `devfs_rules_hide_all` is the conservative starting point.
- **Relying on `devfs.conf` to keep a node secret on a shared lab machine.** *Symptom:* a collaborator changes `devfs.conf` and the node becomes readable by everyone. *Cause:* `devfs.conf` is operator policy; any operator with write access to `/etc` can change it. *Cure:* the driver's own baseline should be safe in the absence of any `devfs.conf` entry. Treat `devfs.conf` as a permission widener, never as a permission tightener relative to a fundamentally safe baseline.

### Pitfalls in Observability

A handful of pitfalls have nothing to do with code but a lot to do with how easy your driver is to debug.

- **Logging every open and close at full volume.** *Symptom:* the kernel message buffer fills with routine driver noise; real errors are harder to find. *Cause:* the driver uses `device_printf` for every `d_open` and `d_close`. *Cure:* gate routine messages with `if (bootverbose)` or remove them entirely once the driver is stable. Leave `device_printf` for lifecycle events and for genuine errors.
- **Not exposing enough sysctls to diagnose unusual states.** *Symptom:* a user reports a bug, you cannot tell what the driver thinks is happening, and adding diagnostics to the driver requires a rebuild and a reload. *Cause:* the sysctl tree is sparse. *Cure:* expose counters generously. `active_fhs`, `open_count`, `read_count`, `write_count`, `error_count` are cheap. Add an `attach_ticks` and a `last_event_ticks` to let operators tell how long the driver has been up and how recently it was active.



## A Final Study Plan

If you want to deepen your grasp of the material beyond the labs and challenges, here is a suggested plan across the week after you finish the chapter.

**Day 1: Re-read a section.** Pick any single section that felt shakiest on first reading and read it again with the companion tree open next to the text. Just read. Do not try to code yet.

**Day 2: Rebuild stage 2 from scratch.** Starting from Chapter 7's stage 2 source, make every change the Chapter 8 stages describe, one commit at a time. Diff your work against the companion tree at each stage.

**Day 3: Break the driver on purpose.** Introduce three different bugs, one at a time: skip the destructor, forget to destroy the alias, return the wrong errno. Predict what each bug does. Run the probes. See whether the failure matches your prediction.

**Day 4: Read `null.c` and `led.c` end-to-end.** Two small drivers, focused on the device-file surface. Write one paragraph on each summarising what you noticed.

**Day 5: Add the status node from Challenge 6.** Implement the read-only status node with a hand-rolled `uiomove`-equivalent for now; Chapter 9 will show the real idiom.

**Day 6: Try the jail lab.** If you have not done Lab 8.7 yet, do it now. Jails are worth the effort to set up because later chapters will assume familiarity.

**Day 7: Move on.** Do not wait to feel like you have "mastered" Chapter 8. You will return to its material naturally as later chapters build on it. The way to become fluent is to keep building; the way to become stuck is to wait for perfection.



## Companion Tree Quick Reference

Because the companion source tree is part of how this chapter teaches, a quick index of what lives where may help you find things during the labs and challenges.

### Driver Stages

- `examples/part-02/ch08-working-with-device-files/stage0-structured-name/` is the Lab 8.1 output: Chapter 7's stage 2 driver with the node moved to `/dev/myfirst/0` and ownership tightened to `root:operator 0660`.
- `examples/part-02/ch08-working-with-device-files/stage1-alias/` is the Lab 8.2 output: stage 0 plus `make_dev_alias("myfirst")`.
- `examples/part-02/ch08-working-with-device-files/stage2-perhandle/` is the Lab 8.3 output: stage 1 plus `devfs_set_cdevpriv` per-open state and removal of the exclusive-open check. This is the driver most of the chapter's other exercises use.
- `examples/part-02/ch08-working-with-device-files/stage3-two-nodes/` is the Lab 8.5 output: adds a control node at `/dev/myfirst/%d.ctl` with its own `cdevsw` and a narrower permission mode.
- `examples/part-02/ch08-working-with-device-files/stage4-destroy-drain/` is the Lab 8.8 exercise: a multi-cdev driver demonstrating the difference between `destroy_dev` alone and `destroy_dev_drain`. Build with `make CFLAGS+=-DUSE_DRAIN=1` for the correct variant.

### Userland Probes

- `userland/probe_myfirst.c`: one-shot open, read, close.
- `userland/hold_myfirst.c`: open and sleep without closing, to exercise the cdevpriv destructor on process exit.
- `userland/stat_myfirst.c`: report `stat(2)` metadata for one or more paths; useful for comparing alias and primary.
- `userland/parallel_probe.c`: open N descriptors from one process, hold, close all.
- `userland/stress_probe.c`: loop open/close to shake leaks out.
- `userland/devd_watch.sh`: subscribe to `devd(8)` events and filter for `myfirst`.

### Configuration Samples

- `devfs/devfs.conf.example`: Lab 8.4 persistence entries.
- `devfs/devfs.rules.example`: Lab 8.7 jail ruleset.
- `devfs/devd.conf.example`: Challenge 5 devd rule.
- `jail/jail.conf.example`: Lab 8.7 jail definition that references ruleset 100.

### How the Stages Differ

Every stage is a diff against Chapter 7's stage 2. A useful first exercise after reading the chapter is to run `diff` between each pair of stages and read the result. The changes are small enough to understand line by line, and the diff tells the progressive story of the chapter's code changes more compactly than re-reading each source file.

```sh
% diff -u examples/part-02/ch07-writing-your-first-driver/stage2-final/myfirst.c \
         examples/part-02/ch08-working-with-device-files/stage0-structured-name/myfirst.c

% diff -u examples/part-02/ch08-working-with-device-files/stage0-structured-name/myfirst.c \
         examples/part-02/ch08-working-with-device-files/stage1-alias/myfirst.c

% diff -u examples/part-02/ch08-working-with-device-files/stage1-alias/myfirst.c \
         examples/part-02/ch08-working-with-device-files/stage2-perhandle/myfirst.c
```

Each diff should be a handful of additions and no unexpected subtractions. If you see surprising changes, the chapter text is where the reasoning is.

### On Re-using This Tree Later

The stages here are not meant to be the "final" driver. They are snapshots that correspond to checkpoints in the chapter. When you continue into Chapter 9, you will edit stage 2 in place, and it will keep growing. By the time you reach the end of Part 2, the driver has evolved into something much richer than any one stage captures. That is the point: each chapter adds a layer, and the companion tree is there to show each layer individually so you can see the progression.



## A Closing Reflection on Interfaces

Every chapter in this book teaches something different, but a few chapters teach something that runs across the whole practice of driver writing. Chapter 8 is one of them. The specific subject is device files, but the broader subject is **interface design**: how do you shape the boundary between a piece of code you control and a world you do not?

The UNIX philosophy has an answer that has survived half a century. Make the boundary look as much like an ordinary file as you can. Let the existing vocabulary of `open`, `read`, `write`, and `close` do the heavy lifting. Choose names and permissions so operators can reason about them without reading your source. Expose only what the user needs, and no more. Clean up after yourself so aggressively that the kernel can tell when you have lost track of something. Document every choice you make with a comment, a `device_printf`, or a sysctl.

None of these principles are unique to device files. They show up again in network interface design, in storage layering, in the kernel's internal APIs, in the userland tools that talk to the kernel. The reason we spent a whole chapter on the small surface under `/dev` is that the same habits, practised here on something tangible and bounded, will serve you in every layer of the kernel you go on to touch.

When you read a driver in `/usr/src/sys` that feels elegant, one of the reasons is almost always that its device-file surface is narrow and honest. When you read a driver that feels tangled, one of the reasons is almost always that its device-file surface was designed in haste, or widened in response to a short-term pressure, and never narrowed again. The goal of this chapter has been to help you notice that difference, and to give you the vocabulary and the discipline to write the first kind of driver rather than the second.



## Wrapping Up

You now understand the layer between your driver and user space well enough to shape it deliberately. Specifically:

- `/dev` is not a directory on disk. It is a devfs view of live kernel objects.
- A `struct cdev` is the kernel-side identity of your node. A vnode is how the VFS reaches it. A `struct file` is how an individual `open(2)` sits in the kernel.
- `mda_uid`, `mda_gid`, and `mda_mode` set the baseline of what `ls -l` shows. `devfs.conf` and `devfs.rules` layer operator policy on top.
- The node's path is whatever your format string says, slashes included. Subdirectories under `/dev` are a normal and welcome way to group related nodes.
- `make_dev_alias(9)` lets one cdev answer to more than one name. Remember to destroy the alias when you tear down the primary.
- `devfs_set_cdevpriv(9)` gives each `open(2)` its own state, with automatic cleanup. This is the tool you will lean on hardest in the next chapter.

The driver you carry into Chapter 9 is the same `myfirst` you started with, but with a cleaner name, a saner set of permissions, and per-open state ready to hold the read positions, byte counters, and small book keeping that real I/O will need. Keep the file open. You will be editing it again soon.

### A Short Self-Check

Before moving on, make sure you can answer each of the following without looking back at the chapter. If any answer is fuzzy, revisit the relevant section before starting Chapter 9.

1. What is the difference between a `struct cdev`, a devfs vnode, and a `struct file`?
2. Where does `make_dev_s(9)` get the ownership and mode for the node it creates?
3. Why does a `chmod` on `/dev/yournode` not survive a reboot?
4. What does `make_dev_alias(9)` do, and how does it differ from `link` in `devfs.conf`?
5. When does the destructor registered with `devfs_set_cdevpriv(9)` run, and when does it *not* run?
6. How would you confirm from userland that two paths resolve to the same cdev?
7. Why is `D_VERSION` required on every `cdevsw`, and what happens when it is missing?
8. When would you choose `make_dev_s` over `make_dev_p`, and why?
9. What guarantees does `destroy_dev(9)` give you about threads currently inside your handlers?
10. If a jail does not see `/dev/myfirst/0` but the host does, where is the policy that hid it, and how would you inspect it?

If you can answer all ten in your own words, the next chapter will feel like a natural continuation rather than a jump.

### Recap Organised by Topic

The chapter covered a lot. Here is a short re-organisation of the material by topic rather than by section, so you can anchor what you learned.

**On the relationship between the kernel and the filesystem:**

- devfs is a virtual filesystem that presents the kernel's live collection of `struct cdev` objects as file-like nodes under `/dev`.
- It has no on-disk storage. Every node reflects something the kernel currently holds.
- It supports only a small, well-defined set of operations on its nodes.
- Changes made interactively (with `chmod`, for instance) do not persist. Persistent policy lives in `/etc/devfs.conf` and `/etc/devfs.rules`.

**On the objects your driver interacts with:**

- A `struct cdev` is the kernel-side identity of a device node. One per node, regardless of how many file descriptors point at it.
- The `struct cdevsw` is the dispatch table your driver provides. It maps each kind of operation to a handler in your code.
- `struct file` and the devfs vnode sit between the user's file descriptor and your cdev. They carry per-open state and route operations.

**On creating and destroying nodes:**

- `make_dev_s(9)` is the modern, recommended way to create a cdev. Fill in a `struct make_dev_args`, pass it in, get a cdev back.
- `make_dev_alias(9)` creates a second name for an existing cdev. Aliases are first-class cdevs; the kernel keeps them in step with the primary.
- `destroy_dev(9)` destroys a cdev synchronously, draining in-flight handlers. Its cousins `destroy_dev_sched` and `destroy_dev_drain` cover deferred and sweep cases respectively.

**On per-open state:**

- `devfs_set_cdevpriv(9)` attaches a driver-provided pointer to the current file descriptor, along with a destructor.
- `devfs_get_cdevpriv(9)` retrieves that pointer inside later handlers.
- The destructor fires exactly once per successful `set` call, when the last reference to the file descriptor drops.
- This is the primary mechanism for per-open bookkeeping in modern FreeBSD drivers.

**On policy:**

- The driver sets a baseline mode, uid, and gid in the call to `make_dev_s`.
- `/etc/devfs.conf` can adjust those per-node on host devfs mounts.
- `/etc/devfs.rules` can define named rulesets that filter and adjust per-mount, typically for jails.
- Three layers can act on the same cdev, and the order matters.

**On userland:**

- `ls -l`, `stat(1)`, `fstat(1)`, `procstat(1)`, `devinfo(8)`, `devfs(8)`, `sysctl(8)`, and `kldstat(8)` are the everyday tools for inspecting and manipulating the surface your driver exposes.
- Small userland C programs that open, read, close, and `stat` the device are worth writing. They give you control over timing and let you test edge cases cleanly.

**On discipline:**

- Default to narrow permissions and widen only when a concrete consumer asks.
- Use named constants (`UID_ROOT`, `GID_WHEEL`) rather than raw numbers.
- Destroy in the reverse order of creation.
- Free allocations on every error path before returning.
- Log lifecycle events with `device_printf(9)` so that `dmesg` tells the story of what your driver is doing.

That is a lot. You do not need to hold it all at once. The labs and challenges are where the material becomes muscle memory; the text is only the reading guide.

### Looking Ahead to Chapter 9

In Chapter 9 we will fill in `d_read` and `d_write` properly. You will learn how the kernel moves bytes between user memory and kernel memory with `uiomove(9)`, why `struct uio` looks the way it does, and how to design a driver that is safe against short reads, short writes, misaligned buffers, and misbehaving user programs. The per-open state you just wired in will carry read offsets and write state. The alias will keep the old user interfaces working while the driver grows. And the permissions model you set up here will keep your lab scripts honest as you start sending real data through.

Specifically, Chapter 9 will need the fields you added to `struct myfirst_fh` for two things. The `reads` counter will grow a matching `read_offset` field so each descriptor remembers where it was in a synthesised data stream. The `writes` counter will be joined by a small ring buffer that `d_write` appends into and `d_read` drains from. The `fh` pointer you retrieve with `devfs_get_cdevpriv` in every handler will be the entry point for all of that state.

The alias you created in Lab 8.2 will keep working without any changes: both `/dev/myfirst` and `/dev/myfirst/0` will produce data, and per-descriptor state will be independent between them.

The permissions you set in Lab 8.1 and Lab 8.4 will remain the correct defaults for development: tight enough to force a conscious `sudo` when a raw user reaches for the device, open enough that a test harness in the `operator` group can run the data-path tests without escalating.

You have built a well-shaped door. In the next chapter, the rooms behind it come alive.



## Reference: make_dev_s and cdevsw at a Glance

This reference collects the most useful declarations and flag values in one place, cross-referenced to the sections of the chapter that explained each one. Keep it open while you write your own drivers; most of the mistakes that cost a day are mistakes about one of these values.

### Canonical make_dev_s Skeleton

A disciplined template for a single-node driver:

```c
struct make_dev_args args;
int error;

make_dev_args_init(&args);
args.mda_devsw   = &myfirst_cdevsw;
args.mda_uid     = UID_ROOT;
args.mda_gid     = GID_OPERATOR;
args.mda_mode    = 0660;
args.mda_si_drv1 = sc;

error = make_dev_s(&args, &sc->cdev, "myfirst/%d", sc->unit);
if (error != 0) {
        device_printf(dev, "make_dev_s: %d\n", error);
        /* unwind and return */
        goto fail;
}
```

### Canonical cdevsw Skeleton

```c
static struct cdevsw myfirst_cdevsw = {
        .d_version = D_VERSION,
        .d_name    = "myfirst",
        .d_open    = myfirst_open,
        .d_close   = myfirst_close,
        .d_read    = myfirst_read,
        .d_write   = myfirst_write,
        .d_ioctl   = myfirst_ioctl,     /* add in Chapter 25 */
        .d_poll    = myfirst_poll,      /* add in Chapter 10 */
        .d_kqfilter = myfirst_kqfilter, /* add in Chapter 10 */
};
```

Fields omitted are equivalent to `NULL`, which the kernel interprets either as "not supported" or "use the default behavior" depending on the field.

### The make_dev_args Structure

From `/usr/src/sys/sys/conf.h`:

```c
struct make_dev_args {
        size_t         mda_size;         /* set by make_dev_args_init */
        int            mda_flags;        /* MAKEDEV_* flags */
        struct cdevsw *mda_devsw;        /* required */
        struct ucred  *mda_cr;           /* usually NULL */
        uid_t          mda_uid;          /* see UID_* in conf.h */
        gid_t          mda_gid;          /* see GID_* in conf.h */
        int            mda_mode;         /* octal mode */
        int            mda_unit;         /* unit number (0..INT_MAX) */
        void          *mda_si_drv1;      /* usually the softc */
        void          *mda_si_drv2;      /* second driver pointer */
};
```

### The MAKEDEV Flag Word

| Flag                   | Meaning                                                 |
|------------------------|---------------------------------------------------------|
| `MAKEDEV_REF`          | Add an extra reference at creation.                     |
| `MAKEDEV_NOWAIT`       | Do not sleep for memory; return `ENOMEM` if tight.      |
| `MAKEDEV_WAITOK`       | Sleep for memory (default for `make_dev_s`).            |
| `MAKEDEV_ETERNAL`      | Mark the cdev as never-to-be-destroyed.                 |
| `MAKEDEV_CHECKNAME`    | Validate the name; return error on bad names.           |
| `MAKEDEV_WHTOUT`       | Create a whiteout entry (stacked filesystems).          |
| `MAKEDEV_ETERNAL_KLD`  | `MAKEDEV_ETERNAL` when static, zero when built as KLD.  |

### The cdevsw d_flags Field

| Flag             | Meaning                                                          |
|------------------|------------------------------------------------------------------|
| `D_TAPE`         | Category hint: tape device.                                      |
| `D_DISK`         | Category hint: disk device (legacy; modern disks use GEOM).      |
| `D_TTY`          | Category hint: TTY device.                                       |
| `D_MEM`          | Category hint: memory device such as `/dev/mem`.                 |
| `D_TRACKCLOSE`   | Call `d_close` for every `close(2)` on every descriptor.         |
| `D_MMAP_ANON`    | Anonymous-mmap semantics for this cdev.                          |
| `D_NEEDGIANT`    | Force Giant-lock dispatch. Avoid in new code.                    |
| `D_NEEDMINOR`    | Driver uses `clone_create(9)` for minor number allocation.       |

### Common UID and GID Constants

| Constant       | Numeric | Purpose                                    |
|----------------|---------|--------------------------------------------|
| `UID_ROOT`     | 0       | Superuser. Default owner for most nodes.   |
| `UID_BIN`      | 3       | Daemon executables.                        |
| `UID_UUCP`     | 66      | UUCP subsystem.                            |
| `UID_NOBODY`   | 65534   | Unprivileged placeholder.                  |
| `GID_WHEEL`    | 0       | Trusted administrators.                    |
| `GID_KMEM`     | 2       | Read access to kernel memory.              |
| `GID_TTY`      | 4       | Terminal devices.                          |
| `GID_OPERATOR` | 5       | Operational tools.                         |
| `GID_BIN`      | 7       | Daemon-owned files.                        |
| `GID_VIDEO`    | 44      | Video framebuffer access.                  |
| `GID_DIALER`   | 68      | Serial-port dial-out programs.             |
| `GID_NOGROUP`  | 65533   | No group.                                  |
| `GID_NOBODY`   | 65534   | Unprivileged placeholder.                  |

### Destruction Functions

| Function                           | When to use                                                    |
|------------------------------------|----------------------------------------------------------------|
| `destroy_dev(cdev)`                | Ordinary, synchronous destruction with drain.                  |
| `destroy_dev_sched(cdev)`          | Deferred destruction when you cannot sleep.                    |
| `destroy_dev_sched_cb(cdev,cb,arg)`| Deferred destruction with a follow-up callback.                |
| `destroy_dev_drain(cdevsw)`        | Wait for all cdevs of a `cdevsw` to finish, before freeing it. |
| `delist_dev(cdev)`                 | Remove a cdev from devfs without fully destroying yet.         |

### Per-Open State Functions

| Function                                   | Purpose                                           |
|--------------------------------------------|---------------------------------------------------|
| `devfs_set_cdevpriv(priv, dtor)`           | Attach private data to the current descriptor.    |
| `devfs_get_cdevpriv(&priv)`                | Retrieve the private data of the current descriptor. |
| `devfs_clear_cdevpriv()`                   | Detach and run the destructor early.              |
| `devfs_foreach_cdevpriv(dev, cb, arg)`     | Iterate all per-open records on a cdev.           |

### Alias Functions

| Function                                             | Purpose                                    |
|------------------------------------------------------|--------------------------------------------|
| `make_dev_alias(pdev, fmt, ...)`                     | Create an alias for a primary cdev.        |
| `make_dev_alias_p(flags, &cdev, pdev, fmt, ...)`     | Create an alias with flags and error return.|
| `make_dev_physpath_alias(...)`                       | Create a topology-path alias.              |

### Reference-Count Helpers

Usually not called directly by drivers. Listed here for recognition.

| Function                         | Purpose                                                |
|----------------------------------|--------------------------------------------------------|
| `dev_ref(cdev)`                  | Acquire a long-lived reference.                        |
| `dev_rel(cdev)`                  | Release a long-lived reference.                        |
| `dev_refthread(cdev, &ref)`      | Acquire a reference for a handler call.                |
| `dev_relthread(cdev, ref)`       | Release the handler call's reference.                  |

### Where to Read More

- `make_dev(9)`, `destroy_dev(9)`, `cdev(9)` manual pages for the API surface.
- `devfs(5)`, `devfs.conf(5)`, `devfs.rules(5)`, `devfs(8)` for the filesystem-layer documentation.
- `/usr/src/sys/sys/conf.h` for the canonical struct and flag definitions.
- `/usr/src/sys/kern/kern_conf.c` for the implementation of the `make_dev*` family.
- `/usr/src/sys/fs/devfs/devfs_vnops.c` for the implementation of `devfs_set_cdevpriv` and friends.
- `/usr/src/sys/fs/devfs/devfs_rule.c` for the rules subsystem.

This reference is kept short on purpose. The chapter is where the reasoning lives; this section is just the lookup table.

### A Condensed Pattern Catalogue

The table below summarises the main patterns the chapter has shown, each paired with the section that explains it in detail. When you are partway through building a driver and need a quick orientation, scan this list first.

| Pattern                                           | Section in the chapter                                |
|---------------------------------------------------|-------------------------------------------------------|
| Create one data node in `attach`, destroy in `detach` | Chapter 7, referenced in Chapter 8 Lab 8.1           |
| Move the node into a subdirectory under `/dev`   | Naming, Unit Numbers, and Subdirectories             |
| Expose both a data node and a control node       | Multiple Nodes Per Device; Lab 8.5                   |
| Add an alias so the driver answers on two paths  | Aliases: One cdev, More Than One Name; Lab 8.2       |
| Widen or narrow permissions at the operator level| Persistent Policy; Lab 8.4                           |
| Hide or expose a node inside a jail              | Persistent Policy; Lab 8.7                           |
| Give each open its own state and counters        | Per-Open State with `devfs_set_cdevpriv`; Lab 8.3    |
| Run pre-open allocation that is safe against crashes | Per-Open State; Challenge 2                     |
| Enforce exclusive open with `EBUSY`              | Error Codes; Recipe 1                                |
| Tear down many cdevs in one detach               | Destroying cdevs Safely; Lab 8.8                     |
| React to node creation in userland via devd      | Exercising Your Device From Userland; Challenge 5    |
| Compare two paths to verify they share a cdev    | Exercising Your Device From Userland                 |
| Expose driver state through sysctl               | Practical Workflows; Chapter 7 reference             |

Each row names a pattern. Each pattern has a short recipe somewhere in the chapter. When you face a design problem, find the row that fits and follow the link back.

### Common errno Values by Operation

A compact cross-reference of which errno values are conventional for which operations. Pair with Section 13.

| Operation                | Common errno returns                                       |
|--------------------------|------------------------------------------------------------|
| `d_open`                 | `0`, `ENXIO`, `EBUSY`, `EACCES`, `EPERM`, `EINVAL`, `EAGAIN`|
| `d_close`                | `0` almost always; log unusual conditions, do not return them |
| `d_read`                 | `0` on success, `ENXIO` if device gone, `EFAULT` for bad buffers, `EINTR` on signal, `EAGAIN` for non-blocking retry |
| `d_write`                | Same family as `d_read`, plus `ENOSPC` for out-of-space    |
| `d_ioctl` (Chapter 25)   | `0` on success, `ENOTTY` for unknown commands, `EINVAL` for bad arguments |
| `d_poll` (Chapter 10)    | Returns a revents mask, not an errno                       |

Your Chapter 8 driver is concerned mostly with the first two rows. Chapter 9 will extend into the third and fourth.

### A Short Glossary of Terms Used in the Chapter

For readers who have not seen every term before, or who want a quick reminder.

- **cdev**: the kernel-side identity of a device file, one per node.
- **cdevsw**: the dispatch table that maps operations to driver handlers.
- **cdevpriv**: per-open state attached to a file descriptor via `devfs_set_cdevpriv(9)`.
- **devfs**: the virtual filesystem that presents cdevs as nodes under `/dev`.
- **mda_***: members of the `make_dev_args` structure passed to `make_dev_s(9)`.
- **softc**: per-device private data allocated by Newbus and reachable through `device_get_softc(9)`.
- **SI_***: flags stored on a `struct cdev` in its `si_flags` field.
- **D_***: flags stored on a `struct cdevsw` in its `d_flags` field.
- **MAKEDEV_***: flags passed to `make_dev_s(9)` and its relatives through `mda_flags`.
- **UID_*** and **GID_***: symbolic constants for standard user and group identities.
- **destroy_dev_drain**: the `cdevsw`-level drain function used when unloading a module that has created many cdevs.
- **devfs.conf**: the host-level policy file for persistent node ownership and mode.
- **devfs.rules**: the ruleset file that shapes per-mount views of devfs, primarily for jails.

The glossary will grow as the book progresses. Chapter 8 introduced most of the terms it will need; later chapters will add their own and reference back to this list.



## Consolidation and Review

Before you put the chapter down, one more pass through the material is worthwhile. This section ties the pieces together in a way that the section-by-section structure could not quite do.

### The Three Ideas That Matter Most

If you could only remember three things from Chapter 8, let them be these:

**First, `/dev` is a live filesystem maintained by the kernel.** Every node is backed by a `struct cdev` your driver owns. Nothing you see in `/dev` is persistent; it is a window into the current state of the kernel. When you write a driver, you are adding to and removing from that window, and the kernel is honest about reflecting your changes.

**Second, the device-file surface is part of your driver's public interface.** The name, the permissions, the ownership, the existence of aliases, the set of operations you implement, the errno values you return, the order of destruction, all of these are decisions a user depends on. Treat them as contractual from day one. Widening or tightening after the fact is always more disruptive than picking the right baseline the first time.

**Third, per-open state is the right home for per-descriptor information.** `devfs_set_cdevpriv(9)` exists because UNIX's descriptor model is more expressive than a single softc can represent. When two processes open the same node, they each deserve their own view of it. Giving them per-open state costs a small allocation and a destructor; the alternative is a maze of shared-state races that you will not want to debug.

Everything else in Chapter 8 elaborates one of these three ideas.

### The Shape of the Driver You End the Chapter With

By the end of Lab 8.8, your `myfirst` driver has grown into something that looks much more like a real FreeBSD driver than it did at the end of Chapter 7. Specifically:

- It has a softc, a mutex, and a sysctl tree.
- It creates its node in a subdirectory under `/dev`, with ownership and mode chosen on purpose.
- It offers an alias for the legacy name so existing users keep working.
- It allocates per-open state on every `open(2)` and cleans it up through a destructor that fires reliably in all cases.
- It counts active opens and refuses to detach while any are still alive.
- It destroys its cdevs in a sensible order during `detach`.

That is the shape of almost every small driver in `/usr/src/sys/dev`. You do not need to build every driver you ever write from scratch; most of the time, you will start from a template that looks exactly like this, and add the subsystem-specific logic on top.

### What to Practice Before Starting Chapter 9

A short list of exercises that cement the chapter's material, in rough order of increasing stretch:

1. **Rebuild `myfirst` stage by stage without looking at the companion tree.** Open Chapter 7's stage 2 source. Make the changes for Lab 8.1 from scratch. Then the changes for Lab 8.2. Then Lab 8.3. Compare your result to the companion tree's stage 2 source. Differences are things worth understanding.
2. **Break a stage on purpose.** Introduce a deliberate bug into Lab 8.3 (for instance, skip the `devfs_set_cdevpriv` call). Predict what will happen when you load and run the parallel probe. Run it. See whether the failure matches your prediction.
3. **Add a third cdev.** Extend Lab 8.5's stage-3 driver with a second control node serving a different namespace. Watch the node appear and disappear in lockstep with the driver.
4. **Write a userland service.** Write a small daemon that opens `/dev/myfirst/0` at startup, holds the descriptor, and responds to SIGUSR1 by reading and logging. Install it. Test it across driver load and unload. Notice what happens when the driver unloads while the daemon still has its descriptor open.
5. **Read a new driver.** Pick a driver from `/usr/src/sys/dev` that you have not touched yet, read it through the device-file lens, and classify it using the decision tree from Section 15. Write a paragraph describing what you found.

Each exercise takes between thirty minutes and an hour. Doing two or three of them is enough to move the chapter's material from "I read this once" to "I am comfortable with it". Doing all five gives you intuition that will serve you for the rest of the book.

Chapter 9 is next. The rooms behind the door come alive.
