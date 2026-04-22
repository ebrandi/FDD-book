---
title: "Reading and Writing to Devices"
description: "How d_read and d_write move bytes safely between user space and the kernel through uio and uiomove."
partNumber: 2
partName: "Building Your First Driver"
chapter: 9
lastUpdated: "2026-04-17"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "TBD"
estimatedReadTime: 195
---

# Reading and Writing to Devices

## Reader Guidance & Outcomes

Chapter 7 taught you to stand a driver up. Chapter 8 taught you how that driver meets userland through `/dev`. The driver you finished the previous chapter with attaches as a Newbus device, creates `/dev/myfirst/0`, carries an alias at `/dev/myfirst`, allocates per-open state, logs cleanly, and detaches without leaking. Every one of those pieces was important, but none of them actually moved a byte.

This chapter is where the bytes start to move.

When a user program calls `read(2)` or `write(2)` on one of your device nodes, the kernel has to deliver real data between the user's address space and the driver's memory. That transfer is not a simple `memcpy`. It crosses a trust boundary. The buffer pointer the user passed might be invalid. The buffer might not all be resident. The length might be zero, or enormous, or part of a scatter-gather list. The user might be in a jail, might have a signal pending, might be reading with `O_NONBLOCK`, might have redirected the result through a pipe. Your driver does not need to understand every one of those cases in isolation, but it does need to cooperate with the single kernel abstraction that solves them all. That abstraction is `struct uio`, and the primary tool for using it is `uiomove(9)`.

This chapter is where we finally implement the `d_read` and `d_write` entry points that Chapter 7 left as stubs. Along the way, we will look carefully at how the kernel describes an I/O request, why the book has been saying "do not touch user pointers directly" ever since Chapter 5, and how to shape a driver so that partial transfers, misaligned buffers, signalled reads, and short writes all behave the way a classical UNIX file would.

### Why This Chapter Earns Its Own Place

It would be tempting to write a short chapter that just says "call `uiomove`" and moves on. That would leave a reader with a driver that passes the simplest test and then fails in twenty subtle ways. The reason this chapter has the length it does is that I/O is where beginner drivers most often go wrong, and the places they go wrong are not where the code looks risky. The mistakes are usually in the return values, in the handling of `uio_resid`, in the treatment of a zero-length transfer, in what happens when the driver wakes up from `msleep(9)` because the process was killed, in the direction in which a partial read should drain.

A driver that gets these details wrong compiles cleanly, passes a single `cat /dev/myfirst`, and then produces corrupted data when a real program starts pushing bytes through it. That is the kind of bug that eats days. The goal of this chapter is to stop that class of bug at the source.

### Where Chapter 8 Left the Driver

At the end of Chapter 8 your `myfirst` driver had the following shape. It is worth a checkpoint because Chapter 9 builds directly on top of it:

- A single Newbus child, created in `device_identify`, registered under `nexus0`.
- A `struct myfirst_softc` allocated by Newbus and initialised in `attach`.
- A mutex named after the device, used to guard softc counters.
- A sysctl tree under `dev.myfirst.0.stats` exposing `attach_ticks`, `open_count`, `active_fhs`, and `bytes_read`.
- A primary cdev at `/dev/myfirst/0` with ownership `root:operator` and mode `0660`.
- An alias cdev at `/dev/myfirst` pointing at the primary.
- A `struct myfirst_fh` allocated per `open(2)`, registered through `devfs_set_cdevpriv(9)`, and freed by a destructor that fires exactly once per descriptor.
- Stub `d_read` and `d_write` handlers that retrieve the per-open state, optionally look at it, and return immediately: `d_read` returns zero bytes (EOF), `d_write` claims to have consumed every byte by setting `uio_resid = 0`.

Chapter 9 takes those stubs and makes them real. The shape of the driver does not change much at the outside. A new reader should still see `/dev/myfirst/0`, still see the alias, still see the sysctls. What changes is that a `cat /dev/myfirst/0` will now produce output, a `echo hello > /dev/myfirst/0` will now store the text into driver memory, and a second `cat` will read back exactly what the first write deposited. By the end of the chapter your driver will be a small, disciplined, in-memory buffer that you can push bytes through and pull bytes out of. It will not yet be a circular buffer with blocking reads; that is Chapter 10's job. It will be a driver that moves bytes correctly.

### What You Will Learn

After finishing this chapter you will be able to:

- Explain how `read(2)` and `write(2)` flow from user space through devfs into your `cdevsw` handlers.
- Read and write the fields of `struct uio` without memorising them.
- Use `uiomove(9)` to transfer bytes between a kernel buffer and the caller's buffer in either direction.
- Use `uiomove_frombuf(9)` when the kernel buffer has a fixed size and you want automatic offset accounting.
- Decide when to reach for `copyin(9)` or `copyout(9)` instead of `uiomove(9)`.
- Return correct byte counts for short transfers, empty transfers, end-of-file, and interrupted reads.
- Choose an appropriate errno value for every error path a driver read or write can take.
- Design an internal buffer that the driver fills from `d_write` and drains from `d_read`.
- Identify and fix the most common `d_read` and `d_write` bugs.
- Exercise the driver from the base-system tools (`cat`, `echo`, `dd`, `od`, `hexdump`) and from a small C program.

### What You Will Build

You will take the `myfirst` driver from the end of Chapter 8 through three incremental stages.

1. **Stage 1, a static-message reader.** `d_read` returns the contents of a fixed kernel-space string. Every open starts at offset zero and reads its way through the message. This is the "hello world" of device reads, but done with correct offset handling.
2. **Stage 2, a write-once / read-many buffer.** The driver owns a fixed-size kernel buffer. `d_write` appends into it. `d_read` returns whatever has been written so far, from a per-descriptor offset that remembers how far each reader has consumed. Two concurrent readers still see their own progress independently.
3. **Stage 3, a small echo driver.** The same buffer, now used as a first-in / first-out store. Every `write(2)` appends bytes to the tail. Every `read(2)` removes bytes from the head. A two-process test script writes in one terminal and reads the echoed data in another. This is the handoff point to Chapter 10, where we will rebuild the same driver around a true circular buffer, add partial-I/O and non-blocking support, and wire up `poll(2)` and `kqueue(9)`.

All three stages compile, load, and behave predictably. You will exercise each one from `cat`, `echo`, and a small userland program called `rw_myfirst.c` that exercises the edge cases `cat` does not reach on its own.

### What This Chapter Does Not Cover

Several topics touch `read` and `write` but are deliberately postponed:

- **Circular buffers and wrap-around**: Chapter 10 implements a real ring buffer. Stage 3 here uses a simple linear buffer so we can keep focus on the I/O path itself.
- **Blocking reads and `poll(2)`**: Chapter 10 introduces `msleep(9)`-based blocking and the `d_poll` handler. This chapter keeps all reads non-blocking at the driver level; an empty buffer produces an immediate zero-byte read.
- **`ioctl(2)`**: Chapter 25 builds out `d_ioctl`. We touch it only where the reader needs to understand why certain control paths belong there rather than in `write`.
- **Hardware registers and DMA**: Part 4 handles bus resources, `bus_space(9)`, and DMA. The memory we read and write in this chapter is ordinary kernel heap, allocated with `malloc(9)` from `M_DEVBUF`.
- **Concurrency correctness under load**: Part 3 is dedicated to race conditions, locking, and verification. We take mutex-protected care where a race would corrupt the stage-3 buffer, but the deeper discussion is deferred.

Staying inside these lines is how we keep the chapter honest. A beginner chapter that drifts into `ioctl`, DMA, and `kqueue` is a beginner chapter that teaches none of them well.

### Estimated Time Investment

- **Reading only**: roughly one hour.
- **Reading plus typing the three stages**: around three hours, including a couple of load / unload cycles per stage.
- **Reading plus all labs and challenges**: five to seven hours over two or three sessions.

Give yourself a fresh lab boot at the start. Do not rush. The stages are small on purpose, and the real value comes from watching `dmesg`, watching `sysctl`, and probing the device from userland after every change.

### Prerequisites

Before starting this chapter, confirm:

- You have a working `myfirst` driver equivalent to the Chapter 8 stage 2 source under `examples/part-02/ch08-working-with-device-files/stage2-perhandle/`. If you have not yet reached the end of Chapter 8, pause here and come back.
- Your lab machine is running FreeBSD 14.3 with a matching `/usr/src`.
- You have been through Chapter 4's discussion of pointers, structures, and memory layout, and Chapter 5's discussion of kernel-space idioms and safety.
- You understand what a `struct cdev` is and how it is related to a `cdevsw`. Chapter 8 covered this in detail.

If you are uncertain about any of these, the rest of the chapter will be harder than it needs to be. Revisit the relevant sections first.

### How to Get the Most Out of This Chapter

Three habits pay off immediately.

First, keep `/usr/src/sys/dev/null/null.c` open in a second terminal. It is the shortest, cleanest, most readable example of `d_read` and `d_write` in the tree. Every idea this chapter introduces appears somewhere in `null.c` in fifty lines or fewer. Real FreeBSD drivers are the textbook; this book is the reading guide.

Second, keep `/usr/src/sys/sys/uio.h` and `/usr/src/sys/sys/_uio.h` open. The declarations there are short and stable. Read them once now so that when the chapter refers to `uio_iov`, `uio_iovcnt`, `uio_offset`, and `uio_resid`, you do not have to trust the prose alone.

Third, rebuild between changes and confirm behaviour from userland before the next change. This is the habit that separates writing drivers from writing prose about drivers. You will run `cat`, `echo`, `dd`, `stat`, `sysctl`, `dmesg`, and a short C program at every checkpoint. Do not skip them. The failure modes this chapter is teaching you to recognise only become visible when you run the code.

### Roadmap Through the Chapter

The sections in order are:

1. A visual map of the full I/O path, from `read(2)` in user space to `uiomove(9)` inside your handler.
2. A short refresher on what `read` and `write` mean in UNIX, and what they mean specifically to a driver writer.
3. The anatomy of `d_read`: its signature, what it is asked to do, and what it is asked to return.
4. The anatomy of `d_write`: the mirror of `d_read`, plus a few details that only apply in the write direction.
5. A reading protocol for unfamiliar handlers, followed by a second real-driver walkthrough (`mem(4)`) to show a different shape.
6. The `ioflag` argument: where it comes from, what bits matter, and why Chapter 9 mostly ignores it.
7. A close look at `struct uio`, the kernel's I/O description object, field by field, including three snapshots of the same uio through a call.
8. `uiomove(9)` and its companions, the functions that actually move the bytes.
9. `copyin(9)` and `copyout(9)`: when to reach for them, and when to leave them alone in favour of `uiomove`. Plus a cautionary case study about structured data.
10. Internal buffers: static, dynamic, and fixed-size. How to choose one, how to own it safely, and the kernel helpers you should recognise.
11. Error handling: the errno values that matter for I/O, how to signal end of file, and how to think about partial transfers.
12. The three-stage `myfirst` implementation, driver source included.
13. A step-by-step trace of `read(2)` from user space through the kernel down to your handler, plus a mirrored write trace.
14. A practical workflow for testing: `cat`, `echo`, `dd`, `truss`, `ktrace`, and the discipline that turns them into a development rhythm.
15. Observability: sysctl, dmesg, and `vmstat -m`, with a concrete snapshot of the driver under light load.
16. Signed, unsigned, and the perils of off-by-one, a short but high-value section.
17. Troubleshooting notes for the mistakes this chapter's material is most likely to produce, and a contrast table of correct versus buggy handler patterns.
18. Hands-on labs (seven) that walk you through each stage and consolidate the observability workflow.
19. Challenge exercises (eight) that stretch the patterns.
20. Wrapping up and a bridge to Chapter 10.

If this is your first time through the chapter, read linearly and do the labs as you hit them. If you are revisiting the material to consolidate, the reference-style sections at the end stand alone.



## A Visual Map of the I/O Path

Before the prose gets deep, a single picture is worth keeping in mind. The diagram below is the path a `read(2)` call takes from a user program down to your driver, and back up to the caller. Every box is a real piece of kernel code you can find under `/usr/src/sys/`. Every arrow is a real function call. None of it is metaphorical.

```text
                         user space
      +----------------------------------------------+
      |   user program                               |
      |                                              |
      |     n = read(fd, buf, 1024);                 |
      |            |                                 |
      |            v                                 |
      |     libc read() wrapper                      |
      |     (syscall trap instruction)               |
      +-------------|--------------------------------+
                    |
     ==============| kernel trust boundary |===============
                    |
                    v
      +----------------------------------------------+
      |  sys_read()                                   |
      |  /usr/src/sys/kern/sys_generic.c              |
      |  - lookup fd in file table                    |
      |  - fget(fd) -> struct file *                  |
      |  - build a uio around buf, count              |
      +-------------|--------------------------------+
                    |
                    v
      +----------------------------------------------+
      |  struct file ops -> vn_read                   |
      |  /usr/src/sys/kern/vfs_vnops.c                |
      +-------------|--------------------------------+
                    |
                    v
      +----------------------------------------------+
      |  devfs_read_f()                               |
      |  /usr/src/sys/fs/devfs/devfs_vnops.c          |
      |  - devfs_fp_check -> cdev + cdevsw            |
      |  - acquire thread-count ref                   |
      |  - compose ioflag from f_flag                 |
      |  - call cdevsw->d_read(dev, uio, ioflag)      |
      +-------------|--------------------------------+
                    |
                    v
      +----------------------------------------------+
      |  YOUR HANDLER (myfirst_read)                  |
      |  - devfs_get_cdevpriv(&fh)                    |
      |  - verify is_attached                         |
      |  - call uiomove(9) to transfer bytes          |
      |            |                                  |
      |            v                                  |
      |     +-----------------------------------+     |
      |     |  uiomove_faultflag()              |     |
      |     |  /usr/src/sys/kern/subr_uio.c     |     |
      |     |  - for each iovec entry           |     |
      |     |    copyout(kaddr, uaddr, n)  ===> |====|====> user's buf
      |     |    decrement uio_resid            |     |
      |     |    advance uio_offset             |     |
      |     +-----------------------------------+     |
      |  - return 0 or an errno                       |
      +-------------|--------------------------------+
                    |
                    v
      +----------------------------------------------+
      |  devfs_read_f continues                       |
      |  - release thread-count ref                   |
      |  - update atime if bytes moved                |
      +-------------|--------------------------------+
                    |
                    v
      +----------------------------------------------+
      |  sys_read finalises                           |
      |  - compute count = orig_resid - uio_resid     |
      |  - return to userland                         |
      +-------------|--------------------------------+
                    |
     ==============| kernel trust boundary |===============
                    |
                    v
      +----------------------------------------------+
      |   user program sees the return value         |
      |   in n                                        |
      +----------------------------------------------+
```

A handful of features of the picture are worth pinning down because they recur throughout the chapter.

**The trust boundary is crossed exactly twice.** Once on the way down (the user enters the kernel via a syscall trap), and once on the way up (the kernel returns control to user space). Everything in between is kernel-only execution. Your handler runs entirely inside the kernel, on a kernel stack, with the user's registers saved out of the way.

**Your handler is the only place the driver's knowledge enters the path.** Everything above it is kernel machinery that works identically for every character device in the tree. Everything below it is `uiomove` and `copyout`, also kernel machinery. Your handler is the single function where the answer to "what bytes should this read produce?" is computed.

**The user's buffer is never touched by your driver directly.** It is touched by `copyout` from inside `uiomove`. Your driver hands `uiomove` a kernel pointer, and `uiomove` is the only code that dereferences the user pointer on your behalf. This is the shape of the trust boundary, drawn as code: user memory is accessed only through the one API that knows how to do it safely.

**Every step has a matching step on the way back up.** The thread-count reference acquired by devfs is released after your handler returns; the uio's state is inspected to compute the byte count; control unwinds through each layer and returns to user space. Understanding this symmetry is what makes reference-counting feel natural rather than arbitrary.

Print this diagram or sketch it on paper. When you read an unfamiliar driver later in the book, refer back to it. Every `d_read` or `d_write` you will ever study sits at exactly this point in the call chain. The differences between drivers are in the handler; the path around the handler is constant.

For `d_write`, the picture is the mirror image. `devfs_write_f` dispatches to `cdevsw->d_write`, your handler calls `uiomove(9)` in the other direction, `uiomove` calls `copyin` instead of `copyout`, and the kernel unwinds back to `write(2)`. Every arrow in the diagram has a twin; every property listed above applies to writes as well.



## Devices in UNIX: A Quick Refresher

It is worth ten minutes of refresher before we start writing code. Chapter 6 introduced the UNIX I/O model at a conceptual level; Chapter 7 put it into practice; Chapter 8 made the device-file surface tidy. All three of those treatments had reasons not to dwell on the behaviour of `read(2)` and `write(2)` themselves, because the drivers in those chapters did not carry real data. Now we do, and a tight refresher sets the stage for everything that follows.

### What Makes a Device Different from a File?

From the outside, they look identical. Both are opened with `open(2)`. Both are read with `read(2)` and written with `write(2)`. Both are closed with `close(2)`. A user program that works on a regular file almost always works on a device file without changes to its source, because the user-space API makes no distinction between them.

From the inside, there are real differences, and a driver author needs to internalise them.

A regular file has a backing store, usually bytes on disk managed by a filesystem. The kernel decides when to read ahead, when to cache, when to flush. The data has a persistent identity; two programs reading byte zero of a file see the same byte. Seeking is cheap and unlimited within the file's size.

A device file has no backing store in the filesystem sense. When a user program reads from it, the driver decides what bytes to produce. When a user program writes to it, the driver decides what to do with them. The data's identity is whatever your driver defines. Two programs reading from the same device do not necessarily see the same bytes; depending on the driver, they may see the same bytes, may see disjoint halves of a single stream, or may see completely independent streams. Seeking may be meaningful, or meaningless, or actively forbidden.

The practical consequence for your `d_read` and `d_write` handlers is that **the driver is the authoritative definition of what `read` and `write` mean** on this device. The kernel will deliver you an I/O request; it will not tell you what to do with it. The conventions UNIX programs expect, a byte stream, consistent return values, honest error codes, end of file as zero bytes returned, are conventions your driver has to honour on purpose. The kernel does not enforce them.

### How UNIX Treats Devices as Streams of Data

The word "stream" is worth pinning down, because it appears in every discussion of UNIX I/O and carries at least three different meanings depending on context.

For our purposes a stream is a **sequence of bytes delivered in order**. Neither the caller nor the driver knows the total length in advance. Either side can stop at any time. The sequence may have a natural end (a file that has been fully read) or may go on indefinitely (a terminal, a network socket, a sensor). The rules are the same either way: the reader requests some number of bytes, the writer requests that some number of bytes be accepted, and the kernel reports how many bytes actually moved.

A stream has no side-effects beyond the data transfer itself. If your driver needs to expose a control surface, a way to change configuration, reset state, or negotiate parameters, that surface does not belong in `read` and `write`. The interface for control is `ioctl(2)`, covered in Chapter 25. Do not smuggle control commands through the data stream. It makes your driver harder to use, harder to test, and harder to evolve.

A stream is unidirectional per call. `read(2)` moves bytes from the driver to the user. `write(2)` moves bytes from the user to the driver. A single system call never does both. If you need duplex behaviour, for example a request-response pattern, you implement it as a write followed by a read, with whatever coordination your driver requires inside it.

### Sequential vs. Random Access

Most drivers produce sequential streams: the bytes come out in the order they arrive, and `lseek(2)` either does nothing interesting or is refused. A terminal, a serial port, a packet-capture device, a log stream, all of these are sequential.

A few drivers are random-access: the caller can address any byte through `lseek(2)`, and the same offset always reads the same data. A memory-disk driver, `/dev/mem`, and a handful of others fit this model. They look more like regular files than like devices in most respects.

A driver author chooses where on this spectrum the driver sits. Your `myfirst` driver will sit on the sequential end for most of this chapter, with one nuance: each open descriptor carries its own read offset, so two processes reading concurrently start from different points in the stream. This is the compromise most small character devices use. It gives each reader a consistent view of what they have consumed, without imposing a true random-access contract on the driver.

The choice shows up in two places in code:

- **Your `d_read` updates `uio->uio_offset`** (which `uiomove(9)` does for you) if and only if the offset is meaningful to you. For a truly sequential device where offset has no meaning, the value is ignored.
- **Your driver either honours or ignores the incoming `uio->uio_offset`** at the start of each read. Sequential drivers ignore it and serve from wherever they are. Random-access drivers treat it as an address into a linear space.

For the three-stage `myfirst` we will treat `uio->uio_offset` as a per-call snapshot of where this descriptor is in the stream and update our internal counters to match.

### The Role of read() and write() in Device Drivers

Inside the kernel, `read(2)` and `write(2)` on a device file eventually call into your `cdevsw->d_read` and `cdevsw->d_write` function pointers. Everything between the system call and your function is devfs and VFS machinery; everything after your function returns is the kernel delivering the result back to userland. Your handler is the single place where the driver-specific answer to "what happens on this call?" is computed.

The handler's job is not complicated in the abstract:

1. Look at the request. How many bytes are being asked for or handed to you?
2. Move the bytes. Use `uiomove(9)` to transfer data between your kernel buffer and the user's.
3. Return a result. A zero for success (with `uio_resid` updated accordingly), or an errno value for failure.

What makes the handler non-trivial is that step 2 is the trust boundary between user memory and kernel memory, and every interaction with user memory has to be safe against misbehaving or malicious user programs. That is why `uiomove(9)` exists. You do not write the safety logic; the kernel does, as long as you ask through the right API.

### Character Devices vs. Block Devices Revisited

Chapter 8 noted that FreeBSD has not shipped block-special device nodes to userland for many years. Storage drivers live in GEOM and publish themselves up as character devices. For the purposes of this chapter, character device is the only shape we care about.

The practical consequence is that everything in this chapter applies to every driver you are likely to write in Parts 2 through 4. `d_read` and `d_write` are the entry points. `struct uio` is the carrier. `uiomove(9)` is the mover. When we get to Part 6 and look at GEOM-backed storage drivers, their data path will look different, but it will still be built out of the same primitives we are studying now.

### Exercise: Classifying Real Devices on Your FreeBSD System

Before the rest of the chapter digs into code, take five minutes on your lab machine. Open a terminal and walk through `/dev`:

```sh
% ls /dev
% ls -l /dev/null /dev/zero /dev/random /dev/urandom /dev/console
```

For each node you see, ask yourself three questions:

1. Is it sequential or random access?
2. If I `cat` it, should it produce any bytes? What bytes?
3. If I `echo something >` it, should anything be visible? Where?

Try a few:

```sh
% head -c 16 /dev/zero | od -An -tx1
% head -c 16 /dev/random | od -An -tx1
% echo "hello" > /dev/null
% echo $?
```

Notice that `/dev/zero` is inexhaustible, `/dev/random` delivers unpredictable bytes, `/dev/null` swallows writes silently and returns success, and none of these three are seekable in a useful sense. Those behaviours are not accidents. They are the `d_read` and `d_write` handlers of those drivers, doing exactly what we are about to study.

If you open `/usr/src/sys/dev/null/null.c` and look at `null_write`, you will see the one-line implementation: `uio->uio_resid = 0; return 0;`. That is a fully functional `write` handler. The driver has announced "I consumed every byte; no error". That is the smallest meaningful write implementation in FreeBSD, and by the end of this chapter you will be able to write it, and many larger ones, without hesitation.



## The Anatomy of `d_read()`

Your driver's read path begins the moment devfs dispatches a call to `cdevsw->d_read`. The signature is fixed, declared in `/usr/src/sys/sys/conf.h`:

```c
typedef int d_read_t(struct cdev *dev, struct uio *uio, int ioflag);
```

Every `d_read` function in the FreeBSD tree has exactly this shape. The three arguments are the complete description of the call:

- `dev` is the `struct cdev *` representing the device node that was opened. In a driver that handles more than one cdev per instance, it tells you which one the call is on. In `myfirst`, where the primary and its alias dispatch through the same handler, both resolve to the same underlying softc via `dev->si_drv1`.
- `uio` is the `struct uio *` that describes the I/O request: what buffers the user provided, how big they are, where in the stream the read is supposed to start, and how many bytes still need to move. We will dissect it in the next section.
- `ioflag` is a bitmask of flags defined in `/usr/src/sys/sys/vnode.h`. The one that matters for non-blocking I/O is `IO_NDELAY`, which is set when the user opened the descriptor with `O_NONBLOCK` (or passed `O_NONBLOCK` later via `fcntl(F_SETFL, ...)`). There are a handful of other flags related to vnode-based filesystem I/O, but for character-device drivers you will usually inspect only `IO_NDELAY`.

The return value is an errno-style integer: zero on success, a positive errno code on failure. It is **not** a byte count. The kernel computes the byte count by looking at how much `uio_resid` decreased during the call and reports that value up to user space as the return value of `read(2)`. This inversion is one of the two or three most important things to internalise from the chapter. `d_read` returns an error code; the number of bytes transferred is implicit in the uio.

### What `d_read` Is Asked to Do

Reduced to a single sentence, the job is: **produce up to `uio->uio_resid` bytes from the device, deliver them through `uiomove(9)` into whatever buffer the `uio` describes, and return zero**.

A few corollaries follow from that sentence and are worth making explicit.

The function may produce fewer bytes than were asked for. Short reads are legitimate and expected. A user program that asked for 4096 bytes and received 17 does not treat that as an error; it treats it as "the driver had 17 bytes to give right now". The number is visible to the caller because `uiomove(9)` decremented `uio_resid` by 17 as it moved the bytes.

The function may produce zero bytes. A zero-byte read is how UNIX reports end of file. If your driver has no more data to give and is not going to have any more, return zero and leave `uio_resid` untouched. The caller sees a zero-byte `read(2)` and knows the stream is done.

The function must not produce more bytes than were asked for. `uiomove(9)` enforces this for you; it will not move more than `MIN(uio_resid, n)` bytes in a single call. If you call `uiomove` repeatedly inside a single `d_read`, make sure your loop also respects `uio_resid`.

The function must return an errno on failure. On success the return value is zero. Non-zero return values are interpreted by the kernel as errors; the kernel propagates them to user space through `errno`. Common values are `ENXIO`, `EFAULT`, `EIO`, `EINTR`, and `EAGAIN`. We will walk through each in the error-handling section.

The function can sleep. `d_read` runs in a process context (the context of the caller), so `msleep(9)` and friends are legal. This is how drivers implement blocking reads that wait for data. We will not use `msleep(9)` in this chapter (Chapter 10 introduces it formally), but it is worth knowing that you have the right to block.

### What `d_read` Is **Not** Asked to Do

A short list of things the handler is explicitly not responsible for, because the kernel or devfs takes care of them:

- **Locating user memory**. The `uio` already describes the target buffer. Your handler does not need to look up page tables or validate addresses.
- **Checking permissions**. The user's credentials were verified by `open(2)`; by the time `d_read` runs, the caller is allowed to read from this descriptor.
- **Counting bytes for the caller**. The kernel computes the byte count from `uio_resid`. You never return a byte count.
- **Enforcing the global size limit**. The kernel has already clamped `uio_resid` to something the system can handle.

Every one of these is a temptation at some point. Resist them all. Each one is a place where a handler can introduce a subtle bug that a correct use of `uiomove` avoids by construction.

### A First Real `d_read`

Here is the smallest useful `d_read` in the FreeBSD tree. It is the `zero_read` function from `/usr/src/sys/dev/null/null.c`, and it is how `/dev/zero` produces an infinite stream of zero bytes:

```c
static int
zero_read(struct cdev *dev __unused, struct uio *uio, int flags __unused)
{
        void *zbuf;
        ssize_t len;
        int error = 0;

        zbuf = __DECONST(void *, zero_region);
        while (uio->uio_resid > 0 && error == 0) {
                len = uio->uio_resid;
                if (len > ZERO_REGION_SIZE)
                        len = ZERO_REGION_SIZE;
                error = uiomove(zbuf, len, uio);
        }
        return (error);
}
```

Pause on that for a minute. The loop body is three lines. The termination condition is two: either `uio_resid` reaches zero (we transferred everything the caller asked for) or `uiomove` returns an error. Each iteration moves as much of the zero-filled region as the request has room for. The function returns the last error code, which is zero if the transfer completed cleanly.

The loop is necessary because the zero region is finite: a single `uiomove` call cannot move arbitrarily many bytes from it, so the loop chunks the transfer. For a driver whose source data fits in a single kernel buffer of modest size, the loop collapses to a single call. Stage 1 of `myfirst` will be exactly that shape.

Notice also what the function does **not** do. It does not look at `uio_offset`. It does not care where in some imaginary stream the read is starting; every read of `/dev/zero` produces zero bytes. It does not check the cdev. It does not check the flags. It does exactly one job, and it does that job using one API.

That is the model. Your `d_read` will usually look like some variation of that loop.

### A Variant: `uiomove_frombuf`

When your source data is a fixed-size kernel buffer and you want the driver to behave like a file backed by that buffer, the helper function `uiomove_frombuf(9)` does the offset arithmetic for you.

Its declaration, from `/usr/src/sys/sys/uio.h`:

```c
int uiomove_frombuf(void *buf, int buflen, struct uio *uio);
```

Its implementation, from `/usr/src/sys/kern/subr_uio.c`, is short enough to reproduce:

```c
int
uiomove_frombuf(void *buf, int buflen, struct uio *uio)
{
        size_t offset, n;

        if (uio->uio_offset < 0 || uio->uio_resid < 0 ||
            (offset = uio->uio_offset) != uio->uio_offset)
                return (EINVAL);
        if (buflen <= 0 || offset >= buflen)
                return (0);
        if ((n = buflen - offset) > IOSIZE_MAX)
                return (EINVAL);
        return (uiomove((char *)buf + offset, n, uio));
}
```

Read that carefully, because the behaviour is precise. The function takes a pointer `buf` to a kernel buffer of size `buflen`, consults the `uio->uio_offset`, and:

- If the offset is negative or otherwise nonsensical, returns `EINVAL`.
- If the offset is past the end of the buffer, returns zero without moving any bytes. This is end of file: the caller will see a zero-byte read.
- Otherwise, calls `uiomove(9)` with a pointer into `buf` at the current offset and a length equal to the remaining tail of the buffer.

The function does not loop; `uiomove` will move as many bytes as `uio_resid` has room for, and will decrement `uio_resid` accordingly. The driver never has to touch `uio_offset` after the fact, because `uiomove` does.

If your driver exposes a fixed buffer as a readable file, a one-line `d_read` suffices:

```c
static int
myfirst_read(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct myfirst_softc *sc = dev->si_drv1;
        return (uiomove_frombuf(sc->buf, sc->buflen, uio));
}
```

Stage 1 of this chapter uses exactly that pattern, with a small adjustment to track the per-descriptor read offset so two concurrent readers see their own progress.

### The Signature in Use: myfirst_read Stage 1

Here is what our Stage 1 `d_read` will look like. Do not type it in yet; we will walk through the full source in the implementation section. Seeing it here and now is mostly to anchor the discussion.

Before you read the code, pause on one detail that will recur in almost every handler for the rest of this chapter. The first four lines of any per-open-aware handler follow a fixed **boilerplate pattern**:

```c
struct myfirst_fh *fh;
int error;

error = devfs_get_cdevpriv((void **)&fh);
if (error != 0)
        return (error);
```

This pattern retrieves the per-descriptor `fh` that `d_open` registered through `devfs_set_cdevpriv(9)`, and it propagates any failure back to the kernel unchanged. You will see it at the top of `myfirst_read`, `myfirst_write`, `myfirst_ioctl`, `myfirst_poll`, and the `kqfilter` helpers. When a later lab says "retrieve the per-open state with the usual `devfs_get_cdevpriv` boilerplate", this is the block it refers to, and the rest of the chapter will not re-explain it. If a handler ever re-orders these lines, treat that as a red flag: running any logic before this call means the handler does not yet know which open it is serving. The one subtlety worth remembering is that the `sc == NULL` liveness check comes *after* this boilerplate, not before, because you need the per-open state retrieved safely even on a device that is being torn down.

```c
static int
myfirst_read(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct myfirst_softc *sc = dev->si_drv1;
        struct myfirst_fh *fh;
        off_t before;
        int error;

        error = devfs_get_cdevpriv((void **)&fh);
        if (error != 0)
                return (error);

        if (sc == NULL || !sc->is_attached)
                return (ENXIO);

        before = uio->uio_offset;
        error = uiomove_frombuf(__DECONST(void *, sc->message),
            sc->message_len, uio);
        if (error == 0)
                fh->reads += (uio->uio_offset - before);
        fh->read_off = uio->uio_offset;
        return (error);
}
```

A few points worth noticing before we continue. The function retrieves the per-open structure through `devfs_get_cdevpriv(9)`, checks that the softc is live, and then hands the real work to `uiomove_frombuf`. We snapshot `uio->uio_offset` into a local `before` at entry so that after the call we can compute the number of bytes the kernel just moved as `uio->uio_offset - before`. That increment is recorded into the per-descriptor counter. The closing assignment to `fh->read_off` remembers the stream position so the rest of the driver can report it later.

If the driver has no data to deliver, `uiomove_frombuf` returns zero and `uio_resid` is unchanged, which is how end of file is reported. If an error occurs inside `uiomove`, we propagate it back up by returning the error code. Nothing about this handler needs `copyin` or `copyout` directly. The safety of the transfer is handled by `uiomove` on our behalf.

### Reading `d_read` in the Tree

A good reading exercise, after finishing this section, is to grep for `d_read` across `/usr/src/sys/dev` and look at what other drivers do inside it. You will find three recurring shapes:

- **Drivers that read from a fixed buffer.** They use `uiomove_frombuf(9)` or a hand-rolled equivalent, one call, done. `/usr/src/sys/fs/pseudofs/pseudofs_vnops.c` uses the helper extensively; the pattern is identical for character devices.
- **Drivers that read from a dynamic buffer.** They take an internal lock, snapshot how much data is available, call `uiomove(9)` with that length, release the lock, and return. We will build one of these in Stage 2.
- **Drivers that read from a blocking source.** They check whether data is available, and if not, either sleep on a condition variable (blocking mode) or return `EAGAIN` (non-blocking mode). This is Chapter 10 territory.

All three shapes share the same four-line spine: retrieve per-open state if you use it, verify liveness, call `uiomove` (or a relative), return the error code. The differences are in how they prepare the buffer, not in how they transfer it.



## The Anatomy of `d_write()`

The write handler is the mirror image of the read handler, with a few small differences at the edges. The signature, from `/usr/src/sys/sys/conf.h`:

```c
typedef int d_write_t(struct cdev *dev, struct uio *uio, int ioflag);
```

The shape is identical. The three arguments carry the same meaning. The return value is an errno, zero on success. The byte count is still computed from `uio_resid`: the kernel looks at how much `uio_resid` decreased during the call and reports that as the return value of `write(2)`.

### What `d_write` Is Asked to Do

One sentence again: **consume up to `uio->uio_resid` bytes from the user, deliver them through `uiomove(9)` into wherever your driver keeps its data, and return zero**.

The corollaries are almost exactly the same as for reads, with two notable differences:

- A short write is legitimate but unusual. A driver that accepts fewer bytes than were offered must update `uio_resid` to reflect the truth, and the kernel will report the partial count to user space. Most well-behaved user programs will loop and retry the remainder; many will not. The rule of thumb is: accept everything you can, and if you cannot accept more, return `EAGAIN` for non-blocking callers and (eventually) sleep for blocking callers.
- A zero-byte write is not end of file. It is simply a write that moved zero bytes. `d_write` does not have an EOF concept; only reads do. A driver that wants to refuse a write returns a non-zero errno.

The most common return value on the error side is `ENOSPC` (no space left on device) when the driver's buffer is full, `EFAULT` when a pointer-related failure occurs inside `uiomove`, and `EIO` for catch-all hardware errors. A driver that enforces a per-write length limit can return `EINVAL` or `EMSGSIZE` for writes that exceed the limit; we will look at which to pick later in the chapter.

### What `d_write` Is **Not** Asked to Do

The same list as for `d_read`: it does not locate user memory, does not check permissions, does not count bytes for the caller, and does not enforce system-wide limits. The kernel takes care of all four.

One addition specific to writes: **do not assume the incoming data is null-terminated or otherwise structured**. Users may write arbitrary bytes. If your driver expects structured input, it must parse it defensively. If your driver expects binary data, it must handle writes that are not aligned with any natural boundary. `write(2)` is a byte stream, not a message queue. Chapter 25's `ioctl` path is where structured, framed commands belong.

### A First Real `d_write`

The simplest non-trivial `d_write` in the tree is `null_write` from `/usr/src/sys/dev/null/null.c`:

```c
static int
null_write(struct cdev *dev __unused, struct uio *uio, int flags __unused)
{
        uio->uio_resid = 0;

        return (0);
}
```

Two lines. The handler tells the kernel "I consumed all the bytes" by setting `uio_resid` to zero, and returns success. The kernel reports the original request length to user space as the number of bytes written. `/dev/null` does not actually do anything with the bytes; that is the whole point of `/dev/null`. But the pattern is instructive: **setting `uio_resid = 0` is the shortest way to mark a write as fully consumed**, and it is exactly what `uiomove(9)` would have done if we had given it a destination.

A marginally more interesting case is `full_write`, also in `null.c`:

```c
static int
full_write(struct cdev *dev __unused, struct uio *uio __unused, int flags __unused)
{
        return (ENOSPC);
}
```

This is the backing for `/dev/full`, a device that is full forever. Every write fails with `ENOSPC`, and the caller sees the corresponding `errno` value. The handler does not touch `uio_resid`; the kernel sees that no bytes moved and reports a return value of -1 with `errno = ENOSPC`.

Together, these two handlers illustrate the two extremes of the write side: accept everything, or reject everything. Real drivers sit somewhere in between, deciding how many of the offered bytes they can accept and storing those bytes somewhere.

### A Write That Actually Stores Data

Here is the shape of the write handler we will implement at the end of this chapter. Again, do not type it in yet; this is a preview for orientation.

```c
static int
myfirst_write(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct myfirst_softc *sc = dev->si_drv1;
        struct myfirst_fh *fh;
        size_t avail, towrite;
        int error;

        error = devfs_get_cdevpriv((void **)&fh);
        if (error != 0)
                return (error);

        if (sc == NULL || !sc->is_attached)
                return (ENXIO);

        mtx_lock(&sc->mtx);
        avail = sc->buflen - sc->bufused;
        if (avail == 0) {
                mtx_unlock(&sc->mtx);
                return (ENOSPC);
        }
        towrite = MIN((size_t)uio->uio_resid, avail);
        error = uiomove(sc->buf + sc->bufused, towrite, uio);
        if (error == 0) {
                sc->bufused += towrite;
                fh->writes += towrite;
        }
        mtx_unlock(&sc->mtx);
        return (error);
}
```

The handler locks the softc mutex, checks how much buffer space is left, clamps the transfer to whatever fits, calls `uiomove(9)` with that length, and accounts for the successful transfer by advancing `bufused`. If the buffer is full, it returns `ENOSPC` to signal the caller. Everything the handler does to cope with concurrency or partial writes is captured in the combination of the lock and the clamp.

Note that `uiomove` itself is called **with the mutex held**. This is fine as long as the mutex is an ordinary `MTX_DEF` mutex (as `myfirst`'s is) and the calling context is a regular kernel thread that can sleep. `uiomove` may page-fault when copying to or from user memory, and page faults may require the kernel to sleep waiting for a disk read. A sleep while holding an `MTX_DEF` mutex is legal; a sleep while holding a spin lock (`MTX_SPIN`) would be a bug. Part 3 covers the locking rules formally; for now, trust the lock type you picked in Chapter 7.

### Symmetry With `d_read`

Reads and writes are almost identical from the driver's point of view. The data flows in opposite directions, and the `uio->uio_rw` field tells `uiomove` which direction to move the bytes. On the driver side you pass the same arguments: a pointer to kernel memory, a length, and the uio. On the user side, `uiomove` either copies out of the kernel buffer (for a read) or into it (for a write). You rarely have to think about the direction; `uio_rw` is already set.

What changes between the two handlers is the **intent**. A read is the driver's opportunity to produce data. A write is the driver's opportunity to consume data. Your code in each handler knows what role it is playing and does the appropriate bookkeeping: a reader tracks how much it has delivered, a writer tracks how much it has stored.

### Reading `d_write` in the Tree

After reading this section, take a few minutes with `grep d_write /usr/src/sys/dev | head -20` and look at what other drivers do. Three shapes show up:

- **Drivers that discard writes**. Usually one line: set `uio_resid = 0` and return zero. The `null` driver's `null_write` is the prototype.
- **Drivers that store writes**. They lock, check capacity, call `uiomove(9)`, account, unlock, and return. Our Stage 3 handler is this shape.
- **Drivers that forward writes to hardware**. They pull data out of the uio, stage it into a DMA buffer or a hardware-owned ring, and trigger the hardware. This shape is out of scope until Part 4; the mechanics of `uiomove` are the same, but the destination is a DMA-mapped region rather than a `malloc`'d buffer.

Every real driver fits one of these three. The ones that accumulate or reshape data first, and then forward it somewhere, tend to combine shape 2 and shape 3, but the primitives are identical.

### Reading an Unfamiliar `d_read` or `d_write` in the Wild

A chapter like this one is most useful when it helps you read other people's code, not only your own. As you explore the FreeBSD tree you will encounter handlers that look nothing like `null_write` or `zero_read`. The shape will still be there; the decoration will differ. Here is a small reading protocol that takes the guesswork out.

**Step one: find the return type and argument names.** Every `d_read_t` and `d_write_t` takes the same three arguments. If the handler has renamed them from `dev`, `uio`, and `ioflag`, note what the author chose (`cdev`, `u`, `flags` are all common). Keep those names in mind as you read.

**Step two: find the `uiomove` call (or relative).** Track backward from there to understand what kernel pointer is being handed to it and what length. That pair is the heart of the handler. Everything before the `uiomove` call is preparing the pointer and length; everything after is accounting.

**Step three: find the lock acquisition and release.** A handler that takes a lock before `uiomove` and releases it after is serializing with other handlers. A handler with no lock is either operating on read-only data or using some other synchronisation primitive (a condition variable, a refcount, a reader lock). Name which it is.

**Step four: find the errno returns.** List the errno values the handler can produce. If the list is short and each value has an obvious trigger, the handler is well-written. If the list is long or opaque, the author probably left loose ends.

**Step five: find the state transitions.** What counters does the handler increment? What per-handle fields does it touch? Those transitions are the driver's behavioural signature, and they are usually the part that differs most between one driver and the next.

Apply this protocol to `zero_read` in `/usr/src/sys/dev/null/null.c`. The argument names are the standard ones. The `uiomove` call hands the kernel pointer `zbuf` (pointing at `zero_region`) and a length clamped by `ZERO_REGION_SIZE`. There is no lock; the data is constant. The only errno the handler can return is whatever `uiomove` returned. There are no state transitions; `/dev/zero` is stateless.

Now apply the same protocol to `myfirst_write` at Stage 3. Argument names: standard. `uiomove` call: kernel pointer `sc->buf + bufhead + bufused`, length `MIN((size_t)uio->uio_resid, avail)`. Lock: `sc->mtx` taken before and released after. Errno returns: `ENXIO` (device gone), `ENOSPC` (buffer full), `EFAULT` via `uiomove`, or zero. State transitions: `sc->bufused += towrite`, `sc->bytes_written += towrite`, `fh->writes += towrite`.

Two drivers, same protocol, two coherent descriptions of what the handler does. Once you apply this reading habit half a dozen times, unfamiliar handlers stop looking unfamiliar.

### What If You Leave `d_read` or `d_write` Unset?

A detail that beginners sometimes wonder about: what happens if your `cdevsw` does not set `.d_read` or `.d_write`? The short answer is that the kernel substitutes a default that returns either `ENODEV` or acts like a no-op, depending on which specific slot is empty and which other `d_flags` are set. The long answer is worth knowing because real drivers do use the defaults, on purpose, when they want to express "this device does not do reads" or "writes are silently discarded".

Look at how `/usr/src/sys/dev/null/null.c` wires up its three drivers:

```c
static struct cdevsw null_cdevsw = {
        .d_version =    D_VERSION,
        .d_read =       (d_read_t *)nullop,
        .d_write =      null_write,
        .d_ioctl =      null_ioctl,
        .d_name =       "null",
};
```

`.d_read` is set to the kernel helper `nullop`, cast to a `d_read_t *`. `nullop` is a universal "do nothing, return zero" function declared in `/usr/src/sys/sys/systm.h` and defined in `/usr/src/sys/kern/kern_conf.c`; it takes no arguments and returns zero. It is used across the kernel wherever a method slot needs a harmless default. The cast works because `d_read_t` expects a function returning `int`, and `nullop`'s `int (*)(void)` shape is close enough for the cdevsw dispatch to call it without surprise.

For `/dev/null`, `(d_read_t *)nullop` means "every read returns zero bytes, forever". A user who `cat /dev/null` sees an immediate EOF. This is different from `/dev/zero`, which installs `zero_read` to produce an infinite stream of zero bytes. The contrast between the two drivers is a contrast between two default read behaviours, and both are exactly one line in the `cdevsw`.

If you omit both `.d_read` and `.d_write` entirely, the kernel fills them with defaults that return `ENODEV`. That is the right choice when the device honestly does not support data transfer; callers see a clear error rather than silent success. But for devices that should quietly accept writes or produce zero-byte reads, setting the slot to `(d_read_t *)nullop` is the idiomatic FreeBSD gesture.

**Practical rule:** decide deliberately. Either implement the handler (for real behaviour), or set it to `(d_read_t *)nullop` / `(d_write_t *)nullop` (for harmless defaults), or leave it entirely unset (for `ENODEV`). Every real driver in the tree picks one of these three on purpose, and the choice is visible to users.

### A Second Real Driver: How `mem(4)` Uses One Handler for Both Directions

`null.c` is the canonical minimal example. A slightly richer example is worth a look before we move on, because it demonstrates a pattern you will meet often in the tree: **a single handler that serves both `d_read` and `d_write`**, relying on `uio->uio_rw` to tell the two directions apart.

The driver is `mem(4)`, which exposes `/dev/mem` and `/dev/kmem`. The common pieces live in `/usr/src/sys/dev/mem/memdev.c`, and the architecture-specific read and write logic lives under `/usr/src/sys/<arch>/<arch>/mem.c`. On amd64, the file is `/usr/src/sys/amd64/amd64/mem.c`, and the function is `memrw`.

Look first at the `cdevsw`:

```c
static struct cdevsw mem_cdevsw = {
        .d_version =    D_VERSION,
        .d_flags =      D_MEM,
        .d_open =       memopen,
        .d_read =       memrw,
        .d_write =      memrw,
        .d_ioctl =      memioctl,
        .d_mmap =       memmmap,
        .d_name =       "mem",
};
```

Both `.d_read` and `.d_write` point at the same function. This is legal because the `d_read_t` and `d_write_t` typedefs are identical (both are `int (*)(struct cdev *, struct uio *, int)`), so a single function can satisfy both. The trick is reading `uio->uio_rw` inside the handler to decide which direction to move.

A condensed sketch of `memrw` looks like this:

```c
int
memrw(struct cdev *dev, struct uio *uio, int flags)
{
        struct iovec *iov;
        /* ... locals ... */
        ssize_t orig_resid;
        int error;

        error = 0;
        orig_resid = uio->uio_resid;
        while (uio->uio_resid > 0 && error == 0) {
                iov = uio->uio_iov;
                if (iov->iov_len == 0) {
                        uio->uio_iov++;
                        uio->uio_iovcnt--;
                        continue;
                }
                /* compute a page-bounded chunk size into c */
                /* ... direction-independent mapping logic ... */
                error = uiomove(kernel_pointer, c, uio);
        }
        /*
         * Don't return error if any byte was written.  Read and write
         * can return error only if no i/o was performed.
         */
        if (uio->uio_resid != orig_resid)
                error = 0;
        return (error);
}
```

There are three ideas in this sketch that generalise to your own drivers.

**First, one handler for both directions saves code when the per-byte work is identical.** The mapping logic in `memrw` resolves a user-space offset to a piece of kernel-accessible memory; whether you are reading from or writing to that memory is decided later, by `uiomove` looking at `uio->uio_rw`. You save yourself the duplication of a near-identical read-vs-write pair at the cost of a single function that has to be clear about which direction it is in. If the two directions share almost nothing, write two functions; if they share nearly everything, combine them.

**Second, `memrw` walks the iovec itself.** Unlike `myfirst`, which hands the whole transfer to `uiomove` in one or two calls, `memrw` walks iovec entries explicitly so that it can map each requested offset into kernel memory and then call `uiomove` on the mapped region. This is the pattern you use when the *kernel pointer* your driver hands to `uiomove` depends on the offset being serviced. It is less common than the `myfirst` style, but it is the right shape when each chunk of the transfer corresponds to a different piece of the driver's backing storage.

**Third, note the orig_resid trick at the end.** The handler saves `uio_resid` at entry, then after the loop checks whether anything at all moved. If something did, it returns zero (success) even if an error happened later, because UNIX conventions require that a read or write with a non-zero byte count return that count to the caller rather than failing the whole call. This is the "partial success" idiom: if any byte moved, report the byte count; only fail when no bytes moved at all.

Your `myfirst` handlers do not need the idiom, because they call `uiomove` exactly once. If `uiomove` succeeds, everything moved; if it fails, nothing moved (from the driver's accounting point of view). The orig_resid idiom matters when your handler loops and the loop can be interrupted mid-way by an error from `uiomove`. Remember the pattern; you will use it in later chapters when your driver serves data from multiple sources.

### Why This Walkthrough Was Worth the Detour

Two drivers. Two very different backing stores. One primitive. In `null.c`, `zero_read` serves a pre-allocated zero region; in `memrw`, the handler serves physical memory mapped on demand. The code looks different in the middle, because the middle is where the driver's unique knowledge lives. The ends look the same: both functions take a uio, both loop on `uio_resid`, both call `uiomove(9)` to do the actual transfer, both return zero on success or an errno.

That uniformity is the point. Every character-device read and write in the tree obeys this shape. Once you recognise it, you can open any unfamiliar driver under `/usr/src/sys/dev` and read the handler with confidence: the part you do not yet understand is always the middle, never the ends.



## Understanding the `ioflag` Argument

Both `d_read` and `d_write` receive a third argument that the rest of the chapter has barely used so far. This section is the short but useful explanation of what `ioflag` is, where it comes from, and when a character-device driver should actually look at it.

### Where `ioflag` Comes From

Every time a process performs a `read(2)` or `write(2)` on a devfs node, the kernel composes an `ioflag` value from the current file-descriptor flags before calling your handler. The composition lives in devfs itself, in `/usr/src/sys/fs/devfs/devfs_vnops.c`. The relevant lines from `devfs_read_f` are:

```c
ioflag = fp->f_flag & (O_NONBLOCK | O_DIRECT);
if (ioflag & O_DIRECT)
        ioflag |= IO_DIRECT;
```

The pattern in `devfs_write_f` is the mirror image. The kernel takes whichever bits of the file table's `f_flag` word are interesting for I/O, masks them out, and passes that subset on as `ioflag`.

This is important for two reasons. First, it means the `ioflag` your driver receives is a *snapshot*. If the user program changes its non-blocking setting (via `fcntl(F_SETFL, O_NONBLOCK)`) between two `read(2)` calls, each call will carry its own up-to-date `ioflag`. You do not need to cache the state or watch for changes; the kernel re-derives the value at every dispatch.

Second, it means most of the constants you might expect to see never reach your handler. Things like `O_APPEND`, `O_TRUNC`, `O_CLOEXEC`, and the various `O_EXLOCK`-style flags belong to the filesystem and file-table layers. They do not influence character-device I/O and are not forwarded.

### The Flag Bits That Matter

The `IO_*` flags are declared in `/usr/src/sys/sys/vnode.h`. For character-device drivers, only a small subset is worth remembering:

```c
#define	IO_UNIT		0x0001		/* do I/O as atomic unit */
#define	IO_APPEND	0x0002		/* append write to end */
#define	IO_NDELAY	0x0004		/* FNDELAY flag set in file table */
#define	IO_DIRECT	0x0010		/* attempt to bypass buffer cache */
```

Of these, **only `IO_NDELAY` and `IO_DIRECT` are composed into the `ioflag` your handler receives**. The first three bits exist for filesystem I/O. A character-device driver that inspects `IO_UNIT` or `IO_APPEND` is looking at values that will always be zero.

`IO_NDELAY` is the common case. It is set when the descriptor is in non-blocking mode. A driver that implements blocking reads (Chapter 10) uses this bit to decide between sleeping and returning `EAGAIN`. A Chapter 9 driver does not sleep on anything, so the bit is informational only, but future chapters rely on it.

`IO_DIRECT` is a hint that the user program opened the descriptor with `O_DIRECT`, asking the kernel to bypass buffer caches where possible. For a simple character driver it is almost always irrelevant. Storage-adjacent drivers may choose to honour it; most do not.

Note the numerical identity: `O_NONBLOCK` in `/usr/src/sys/sys/fcntl.h` has the value `0x0004`, and `IO_NDELAY` in `/usr/src/sys/sys/vnode.h` has the same value. That is not an accident. The header comment above the `IO_*` definitions explicitly states that `IO_NDELAY` and `IO_DIRECT` are aligned with the corresponding `fcntl(2)` bits so that devfs does not need to translate. Your driver can inspect the bit either way and get the same answer.

### A Handler That Checks `ioflag`

Here is what a non-blocking-aware read handler looks like at the skeleton level. We will not use this shape in Chapter 9 because we never sleep, but studying it now makes Chapter 10's introduction quicker.

```c
static int
myfirst_read_nb(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct myfirst_softc *sc = dev->si_drv1;
        int error;

        mtx_lock(&sc->mtx);
        while (sc->bufused == 0) {
                if (ioflag & IO_NDELAY) {
                        mtx_unlock(&sc->mtx);
                        return (EAGAIN);
                }
                /* ... would msleep(9) here in Chapter 10 ... */
        }
        /* ... drain buffer, uiomove, unlock, return ... */
        error = 0;
        mtx_unlock(&sc->mtx);
        return (error);
}
```

The branch on `IO_NDELAY` is the only decision the handler makes about blocking. Everything else in the function is ordinary I/O code. That narrowness is part of why `ioflag` is a single integer: a driver's response to flag bits is usually a single `if` statement near the top of the handler, not a sprawling state machine.

### What the Chapter 9 Stages Do With `ioflag`

Stage 1, Stage 2, and Stage 3 do **not** inspect `ioflag`. They cannot block, so the non-blocking bit is moot; they do not care about `IO_DIRECT`. The argument is present in their handler signatures because the typedef demands it, and it is silently ignored.

Silently ignoring an argument is not a bug when the ignored behaviour is obviously correct. A reader who opens one of our descriptors with `O_NONBLOCK` will see identical behaviour to a reader who did not: neither call ever sleeps, so the flag has no observable effect. Chapter 10 is where we will wire the flag through.

### A Small Debugging Aid

If you are curious what `ioflag` contains during a test, a single `device_printf` at entry will tell you:

```c
device_printf(sc->dev, "d_read: ioflag=0x%x resid=%zd offset=%jd\n",
    ioflag, (ssize_t)uio->uio_resid, (intmax_t)uio->uio_offset);
```

Load the driver, run `cat /dev/myfirst/0`, and observe the hex value. Then run a small program that uses `fcntl(fd, F_SETFL, O_NONBLOCK)` before reading, and observe the difference. This is an instructive two-minute detour when you are first making the mechanism real in your head.

### `ioflag` in the Tree

Search `/usr/src/sys/dev` for `IO_NDELAY` and you will find dozens of hits. Almost every one of them is the same pattern: check the bit, return `EAGAIN` if set and the driver has nothing to serve, otherwise sleep. The uniformity is deliberate. FreeBSD drivers treat non-blocking I/O the same way whether they are pseudo-devices, TTY lines, USB endpoints, or GEOM-backed storage, and that consistency is part of why user programs written for one kind of device port cleanly to another.



## Understanding `struct uio` Deeply

`struct uio` is the kernel's representation of an I/O request. It is passed to every `d_read` and `d_write` invocation. Every successful `uiomove(9)` call mutates it. Every driver author you will ever meet has at some point stared at its fields and wondered which ones to trust. This section is where we make the structure less mysterious.

### The Declaration

From `/usr/src/sys/sys/uio.h`:

```c
struct uio {
        struct  iovec *uio_iov;         /* scatter/gather list */
        int     uio_iovcnt;             /* length of scatter/gather list */
        off_t   uio_offset;             /* offset in target object */
        ssize_t uio_resid;              /* remaining bytes to process */
        enum    uio_seg uio_segflg;     /* address space */
        enum    uio_rw uio_rw;          /* operation */
        struct  thread *uio_td;         /* owner */
};
```

Seven fields. Each one has a specific purpose, and there is exactly one function, `uiomove(9)`, that uses all of them in concert. Your driver will read some of the fields directly; a few fields you will never touch.

### `uio_iov` and `uio_iovcnt`: The Scatter-Gather List

A single `read(2)` or `write(2)` operates on one contiguous user buffer. The related `readv(2)` and `writev(2)` operate on a list of buffers (an "iovec"). The kernel represents both cases uniformly as a list of `iovec` entries, using a list of length one for the simple case.

`uio_iov` points at the first entry of that list. `uio_iovcnt` is the number of entries. Each entry is a `struct iovec`, declared in `/usr/src/sys/sys/_iovec.h`:

```c
struct iovec {
        void    *iov_base;
        size_t   iov_len;
};
```

`iov_base` is a pointer into user memory (for a `UIO_USERSPACE` uio) or kernel memory (for a `UIO_SYSSPACE` uio). `iov_len` is the number of bytes remaining in that entry.

You will almost never touch these fields directly. `uiomove(9)` walks the iovec list for you, consuming entries as it moves bytes, and leaving the list consistent with the remaining transfer. If your driver reaches for `uio_iov` or `uio_iovcnt` by hand, you are either writing a very unusual driver or doing something wrong. The conventional pattern is: let `uiomove` manage the iovec, and read the other fields to understand the state of the request.

### `uio_offset`: The Offset Into the Target

For a read or write on a regular file, `uio_offset` is the position in the file where the I/O is taking place. The kernel increments it as bytes move, so a sequential `read(2)` naturally advances through the file.

For a device file, the meaning of `uio_offset` is defined by the driver. A device that is truly sequential and has no notion of position will ignore the incoming value and leave the outgoing value to reflect whatever `uiomove` did. A device that backs itself with a fixed buffer will treat the offset as an address into that buffer and honour it.

`uiomove(9)` updates `uio_offset` in lockstep with `uio_resid`: for every byte it moves, it decrements `uio_resid` by one and increments `uio_offset` by one. If your driver calls `uiomove` once per handler, you rarely need to read `uio_offset` by hand. If your driver calls `uiomove` more than once, or if it uses the offset to index into its own buffer, `uiomove_frombuf(9)` is the helper you want.

### `uio_resid`: The Remaining Bytes

`uio_resid` is the number of bytes that still need to move. At the start of `d_read`, it is the total length the user asked for. At the end of a successful transfer, it is whatever did not move; the kernel subtracts this from the original length to produce the return value of `read(2)`.

Two signed-arithmetic traps are worth calling out. First, `uio_resid` is an `ssize_t`, which is signed. A negative value is illegal (and `uiomove` will `KASSERT` on it in debug kernels), but beware of accidentally constructing one through sloppy arithmetic. Second, `uio_resid` can be zero at the start of the call. This happens when a user program calls `read(fd, buf, 0)` or `write(fd, buf, 0)`. Your handler must not treat zero as "no user intent" and then proceed to do I/O against what might be an uninitialised buffer. The safe pattern is to check for zero early and return zero (or accept zero and return zero, for writes). `uiomove` handles this cleanly: it returns zero immediately without touching anything. So in practice the "check early" is often redundant; what matters is that you do not *assume* it is non-zero.

### `uio_segflg`: Where the Buffer Lives

This field says where the iovec pointers refer to: user space (`UIO_USERSPACE`), kernel space (`UIO_SYSSPACE`), or a direct object map (`UIO_NOCOPY`). The enumeration is in `/usr/src/sys/sys/_uio.h`:

```c
enum uio_seg {
        UIO_USERSPACE,          /* from user data space */
        UIO_SYSSPACE,           /* from system space */
        UIO_NOCOPY              /* don't copy, already in object */
};
```

For a `d_read` or `d_write` called on behalf of a user syscall, `uio_segflg` is `UIO_USERSPACE`. `uiomove(9)` reads the field and picks the right transfer primitive: `copyin` / `copyout` for user-space segments, `bcopy` for kernel-space segments. Your driver does not need to branch on this; `uiomove` does it for you.

You will occasionally see code that builds a kernel-mode uio by hand, typically to re-use a function that takes a uio but serve it from a kernel buffer. That code sets `uio_segflg` to `UIO_SYSSPACE`. It is legitimate and useful, and we will meet it briefly in the labs. Do not confuse it with a user-space uio: the safety properties are very different.

### `uio_rw`: The Direction

The direction of transfer. The enumeration is in the same header:

```c
enum uio_rw {
        UIO_READ,
        UIO_WRITE
};
```

For a `d_read` handler, `uio_rw` is `UIO_READ`. For a `d_write` handler, `uio_rw` is `UIO_WRITE`. The field tells `uiomove` whether to copy kernel->user (read) or user->kernel (write). Some handlers assert on this as a sanity check:

```c
KASSERT(uio->uio_rw == UIO_READ,
    ("Can't be in %s for write", __func__));
```

That assertion is from `zero_read` in `/usr/src/sys/dev/null/null.c`. It is a cheap way to document the invariant. Your driver does not need assertions like this to be correct, but they can be a useful safety net during development.

### `uio_td`: The Owning Thread

The `struct thread *` of the caller. For a uio built on behalf of a syscall, this is the thread that made the syscall. Some kernel APIs want a thread pointer; using `uio->uio_td` rather than `curthread` keeps the association explicit when the uio is handed around.

In a straightforward `d_read` or `d_write`, you rarely need `uio_td`. It becomes useful if your driver wants to inspect the credentials of the caller mid-call, beyond what `open(2)` already validated. That is uncommon.

### An Illustration: What Happens to uio During a read(fd, buf, 1024) Call

Walking through a single `read(2)` helps cement how the fields move. Suppose a user program calls:

```c
ssize_t n = read(fd, buf, 1024);
```

The kernel constructs a uio that looks roughly like this when it reaches your `d_read`:

- `uio_iov` points to a one-entry list.
- The one entry has `iov_base = buf` (the user's buffer) and `iov_len = 1024`.
- `uio_iovcnt = 1`.
- `uio_offset = <wherever the current file pointer was>`. For a newly opened seekable device, zero.
- `uio_resid = 1024`.
- `uio_segflg = UIO_USERSPACE`.
- `uio_rw = UIO_READ`.
- `uio_td = <calling thread>`.

Your handler calls, say, `uiomove(sc->buf, 300, uio)`. Inside `uiomove`, the kernel:

- Takes the first iovec entry.
- Determines that 300 is less than 1024, so it will move 300 bytes.
- Calls `copyout(sc->buf, buf, 300)`.
- Decrements `iov_len` by 300, to 724.
- Advances `iov_base` by 300, to `buf + 300`.
- Decrements `uio_resid` by 300, to 724.
- Increments `uio_offset` by 300.

Your handler returns zero. The kernel computes the byte count as `1024 - 724 = 300` and returns 300 from `read(2)`. The user sees 300 bytes in `buf[0..299]` and knows to either call `read(2)` again to get the rest, or proceed with what they have.

That is everything `uiomove` is doing, in order. There is no magic.

### How `readv(2)` Differs

If the user called `readv(fd, iov, 3)` with three iovec entries, the uio at the start of `d_read` would have `uio_iovcnt = 3`, `uio_iov` pointing at the list of three entries, and `uio_resid` equal to the sum of their lengths. Your handler makes one `uiomove` call (or several, in a loop) and `uiomove` walks the list for you. The driver code is identical.

This is one of the quiet benefits of the uio abstraction: scatter-gather reads and writes are free. Your driver was written for a single buffer; it already handles multi-buffer requests.

### Re-entering a Handler With a Partly Consumed uio

A convention that occasionally trips beginners: **a single `d_read` or `d_write` invocation can make multiple `uiomove` calls**. Each call shrinks `uio_resid` and advances `uio_iov`. The uio remains consistent between calls. If your handler's first `uiomove` moves 128 bytes and the next moves 256, the kernel just sees a single handler call that transferred 384 bytes.

What you should **not** do is save a uio pointer across a handler call and try to resume it later. A uio is valid for the duration of the dispatch that produced it. Between dispatches, the memory it points at (including the iovec array) may not be valid. If you need to queue a request for later processing, you copy the necessary data out of the uio (into your own kernel buffer) and use your own queue.

### What Your Driver Needs to Read and What It Should Leave Alone

A short crib sheet, in decreasing order of frequency:

| Field          | Read it?    | Write it?                         |
|----------------|-------------|-----------------------------------|
| `uio_resid`    | Yes, often  | Only to mark a transfer consumed (e.g., `uio_resid = 0`) |
| `uio_offset`   | Yes, if you honour it | No, let `uiomove` update it |
| `uio_rw`       | Occasionally, for KASSERTs | No |
| `uio_segflg`   | Rarely       | No, unless building a kernel-mode uio |
| `uio_td`       | Rarely       | No |
| `uio_iov`      | Almost never | Never |
| `uio_iovcnt`   | Almost never | Never |

If a beginner-level driver writes to `uio_iov` or `uio_iovcnt`, something has gone seriously off the rails. If it writes to `uio_resid` other than the `uio_resid = 0` "I consumed everything" trick, something is slightly off. If it reads from the first three rows, it is on the normal path.

### The uio Fields in Practice

All of this is less intimidating once you have seen a handler actually use it. The myfirst stages in this chapter inspect `uio_resid` (to clamp transfers), occasionally read `uio_offset` (to know where a reader is), and hand everything else to `uiomove`. The helpers do the real work, and the driver code stays small.

### The Life of a Single uio: Three Snapshots

To cement the field-by-field discussion, it is worth walking through a uio's state at three points in its life: the moment your handler is called, the moment after a partial `uiomove`, and the moment just before your handler returns. Each snapshot captures the same uio, so you can see exactly how the fields evolve.

The example is a `read(fd, buf, 1024)` call on a driver whose read handler will serve 300 bytes per call.

**Snapshot 1: At the entry of `d_read`.**

```text
uio_iov     -> [ { iov_base = buf,       iov_len = 1024 } ]
uio_iovcnt  =  1
uio_offset  =  0        (this is the first read on the descriptor)
uio_resid   =  1024
uio_segflg  =  UIO_USERSPACE
uio_rw      =  UIO_READ
uio_td      =  <calling thread>
```

The uio describes a complete request. The user asked for 1024 bytes, the buffer is in user space, the direction is read, the offset is zero. This is what the kernel hands your handler.

**Snapshot 2: After `uiomove(sc->buf, 300, uio)` returns success.**

```text
uio_iov     -> [ { iov_base = buf + 300, iov_len =  724 } ]
uio_iovcnt  =  1
uio_offset  =  300
uio_resid   =  724
uio_segflg  =  UIO_USERSPACE    (unchanged)
uio_rw      =  UIO_READ         (unchanged)
uio_td      =  <calling thread> (unchanged)
```

Four fields changed in lockstep. `iov_base` advanced by 300 so the next transfer will place bytes after the ones just written. `iov_len` shrunk by 300, because the iovec entry now describes only the remaining 724 bytes. `uio_offset` grew by 300, because 300 bytes of stream position moved. `uio_resid` shrunk by 300, because 300 bytes of work are done.

Three fields stayed fixed: `uio_segflg`, `uio_rw`, and `uio_td` describe the *shape* of the request, which does not change mid-transfer. If your handler ever needs to check one of them, it can do so before or after `uiomove` and get the same answer.

**Snapshot 3: Just before `d_read` returns.**

Imagine the handler, having served 300 bytes, decides it has no more data and returns zero without calling `uiomove` again.

```text
uio_iov     -> [ { iov_base = buf + 300, iov_len =  724 } ]
uio_iovcnt  =  1
uio_offset  =  300
uio_resid   =  724
uio_segflg  =  UIO_USERSPACE
uio_rw      =  UIO_READ
uio_td      =  <calling thread>
```

Identical to Snapshot 2. The handler did not touch anything; it just returned. The kernel will see `uio_resid = 724` versus the starting `uio_resid = 1024` and compute `1024 - 724 = 300`, which it returns to user space as the result of `read(2)`. The caller sees a return value of 300 and knows the driver produced 300 bytes.

If the handler had looped on `uiomove` until `uio_resid` reached zero, the snapshot at return would instead have `uio_resid = 0` and the kernel would return 1024 to user space (a full transfer). If the handler had called `uiomove` and got an error, `uio_resid` would reflect whatever partial progress happened before the fault, and the handler would return the errno.

### What This Mental Model Buys You

Three observations fall out of the snapshots that are worth naming explicitly.

**First, uio_resid is the contract.** Whatever value is in `uio_resid` when your handler returns, the kernel will trust. If it is smaller than at entry, some bytes moved; the difference is the byte count. If it is unchanged, nothing moved; the return value will be zero (EOF) or an errno (depending on what your handler returned).

**Second, uiomove is the only thing you should rely on to decrement uio_resid.** A driver that manually subtracts from `uio_resid` is almost certainly doing something wrong; the kernel's fault handling, iovec walking, and offset updates are all baked into the `uiomove` code path. Setting `uio_resid = 0` is the one exception, used by drivers like `null.c`'s `null_write` to say "pretend all the bytes were consumed".

**Third, the uio is scratch space.** A uio is not a long-lived object. It is created per syscall, decays as `uiomove` consumes it, and is discarded when your handler returns. Saving a uio pointer for later use is a lifetime bug waiting to fire. If your driver needs data from the uio beyond the current call, it copies the bytes out into its own storage (which is what `d_write` does: it copies bytes through `uiomove` into `sc->buf`, leaving nothing in the uio for later).

These three facts are the foundation everything else in the chapter is built on. If you internalise them, the rest of the uio machinery stops looking mysterious.



## Safe Data Transfer: `uiomove`, `copyin`, `copyout`

The previous sections described `struct uio` and named `uiomove(9)` as the function that moves bytes. This section explains why that function exists, what it does under the hood, and when a driver should reach for `copyin(9)` or `copyout(9)` directly instead.

### Why Direct Memory Access Is Unsafe

A user process has its own virtual address space. When a process calls `read(2)` with a buffer pointer, that pointer is a virtual address in the process's address space. It may refer to a page of memory that is present in physical RAM, or to a page that has been swapped out, or to a page that is not mapped at all. It may even be a pointer the user program fabricated deliberately to try to make the kernel crash.

From the kernel's point of view, the user's address space is not directly addressable. The kernel has its own address space; a user pointer handed to the kernel is not meaningful as a kernel pointer. Even if the kernel can resolve the user pointer through page-table machinery, using it directly is dangerous: the page might fault, the memory protection might be wrong, the address might fall outside the process's mapped regions, or the pointer might have been constructed to point at kernel memory in an attempt to leak or corrupt it.

Direct memory access, in other words, is not a feature the kernel gets for free. It is a privilege that must be exercised carefully, with every access routed through functions that know how to handle faults, check protection, and keep the user and kernel address spaces distinct.

On FreeBSD those functions are `copyin(9)` (user to kernel), `copyout(9)` (kernel to user), and `uiomove(9)` (either direction, driven by the uio).

### What `copyin(9)` and `copyout(9)` Do

From `/usr/src/sys/sys/systm.h`:

```c
int copyin(const void * __restrict udaddr,
           void * __restrict kaddr, size_t len);

int copyout(const void * __restrict kaddr,
            void * __restrict udaddr, size_t len);
```

`copyin` takes a user-space pointer, a kernel-space pointer, and a length. It copies `len` bytes from user to kernel. `copyout` is the reverse: kernel pointer, user pointer, length. Copies kernel to user.

Both functions validate the user address, fault in the user page if necessary, perform the copy, and catch any fault that occurs. They return zero on success and `EFAULT` if the user address was invalid or the copy otherwise failed. They never silently corrupt memory; they always either complete the copy or report the error.

These two primitives are the foundation on which all user / kernel memory transfers are built. They are what `uiomove(9)` calls underneath when the uio is in user space. They are what `fubyte(9)`, `subyte(9)`, and a handful of other convenience functions use. They are the functions the kernel trusts to be the trust boundary.

### What `uiomove(9)` Does

`uiomove(9)` is a wrapper around `copyin` / `copyout` that understands the uio structure. Its implementation is short and worth reading; it lives in `/usr/src/sys/kern/subr_uio.c`.

Roughly, the algorithm is:

1. Sanity-check the uio: the direction is valid, the resid is non-negative, the owning thread is the current thread if the segment is user-space.
2. Loop: while the caller has asked for more bytes (`n > 0`) and the uio still has room (`uio->uio_resid > 0`), consume the next iovec entry.
3. For each iovec entry, compute how many bytes to move (the minimum of the entry length, the caller's remaining count, and the uio's resid), and call `copyin` or `copyout` (for user-space segments) or `bcopy` (for kernel-space segments), depending on `uio_rw` and `uio_segflg`.
4. Advance the iovec's `iov_base` and `iov_len` as bytes move; decrement `uio_resid`, increment `uio_offset`.
5. If any copy fails, break out and return the error.

The function returns zero on success or an errno code on failure. The most common failure is `EFAULT` from a bad user pointer.

The critical property of `uiomove` is that it is **the only function your driver should use to move bytes through a uio**. Not `bcopy`, not `memcpy`, not `copyout`. The uio carries the information `uiomove` needs to pick the right primitive, and the driver does not need to second-guess.

### When to Use Which

The division of labour is straightforward in practice.

Use `uiomove(9)` in `d_read` and `d_write` handlers whenever the uio describes the transfer. This is the overwhelmingly common case.

Use `copyin(9)` and `copyout(9)` directly when you have a user pointer from somewhere other than a uio. Examples:

- Inside a `d_ioctl` handler, for control commands that carry user-space pointers as arguments (Chapter 25).
- Inside a kernel thread that accepts user-supplied data through a mechanism you built yourself, not through a uio.
- When reading or writing a small, fixed-size piece of user memory that is not the subject of the syscall.

Do **not** use `copyin` or `copyout` inside `d_read` or `d_write` to fetch from the uio's iovec. Always go through `uiomove`. The iovec is not guaranteed to be a single contiguous buffer, and even if it is, your driver has no business reaching past the uio abstraction to touch it directly.

### A Table for Quick Reference

| Situation                                         | Preferred tool         |
|---------------------------------------------------|------------------------|
| Transferring bytes through a uio (read or write)  | `uiomove(9)`           |
| Transferring bytes through a uio, with a fixed kernel buffer and automatic offset | `uiomove_frombuf(9)` |
| Reading a known user pointer not carried by a uio | `copyin(9)`            |
| Writing to a known user pointer not carried by a uio | `copyout(9)`         |
| Reading a null-terminated string from user space  | `copyinstr(9)`         |
| Reading a single byte from user space             | `fubyte(9)`            |
| Writing a single byte to user space               | `subyte(9)`            |

`fubyte` and `subyte` are niche; most drivers never use them. They are listed here for recognition. `copyinstr` is occasionally useful in control paths that take a user string; we will not reach for it in this chapter.

### Why Not a Straight memcpy?

A beginner sometimes asks "can I just cast the user pointer and `memcpy` the bytes?" The answer is an unqualified no, and it is worth understanding why.

`memcpy` assumes both pointers refer to accessible memory in the current address space. A user pointer is not guaranteed to refer to accessible memory. On architectures that separate user and kernel pointers at the hardware level (SMAP on amd64, for example), the CPU will refuse the access. On architectures that share the address space, the pointer may still be invalid, or may refer to a page that has been swapped out, or may refer to a page the kernel is forbidden to touch. None of these cases are safe to handle inside a plain `memcpy`; the resulting fault would either panic the system or leak information across the trust boundary.

The kernel primitives `copyin` and `copyout` exist precisely to handle these cases correctly. They install a fault handler before the access, so a bad user pointer returns `EFAULT` instead of panicking. They respect SMAP and similar protections. They can wait for the page to be paged in. None of that is optional, and none of it is something your driver should replicate.

The practical rule: if a pointer came from user space, route it through `copyin` / `copyout` / `uiomove`. Do not dereference it directly. Do not `memcpy` through it. Do not pass it to any function that will `memcpy` through it. If you stop at the abstraction boundary, the kernel gives you a stable, safe, well-documented interface. If you cross it, you own every bug forever.

### What Happens Under a Fault

Something concrete: what does `uiomove` actually do when the user pointer is bad?

The kernel installs a fault handler before the copy, typically through its trap-handler table. When the CPU takes a fault on the user access, the fault handler notices that the faulting instruction is inside a `copyin` or `copyout` code path, skips ahead to the failure-return path, and returns `EFAULT`. No panic. No data corruption. The caller of `uiomove` sees a non-zero return value, propagates it to the caller of `d_read` or `d_write`, and the syscall returns to userland with `errno = EFAULT`.

The driver does not need to do anything special to cooperate with this machinery. It just needs to check the return value of `uiomove` and propagate errors. We will do that in every handler in the chapter.

### Alignment and Type Safety

One more subtlety worth naming. The user's buffer is a stream of bytes. It carries no type information. If your driver puts a `struct` into the buffer and the user pulls it out, the user gets bytes; those bytes may or may not be correctly aligned for a `struct` access on the caller's architecture.

For `myfirst` the problem does not arise, because the bytes are arbitrary user text. For drivers that want to export structured data, the convention is either to require the user to memcpy the bytes into an aligned local structure before interpreting them, or to include an explicit alignment and version negotiation in the data format. `ioctl(2)` sidesteps the problem because its data layout is part of the `IOCTL` command number; `read` and `write` do not have that luxury.

This is one of the places where bolting structured data onto `read`/`write` is tempting and wrong. If your driver wants to hand typed data to userland, the `ioctl` interface or an external RPC mechanism are the right tools. `read` and `write` carry bytes. That is the promise, and the promise is what keeps them portable.

### A Small Worked Example

Suppose a user program writes four integers to your device:

```c
int buf[4] = { 1, 2, 3, 4 };
write(fd, buf, sizeof(buf));
```

In the driver's `d_write`, the uio looks like:

- One iovec entry with `iov_base = <user address of buf[0]>`, `iov_len = 16`.
- `uio_resid = 16`, `uio_offset = 0`, `uio_segflg = UIO_USERSPACE`, `uio_rw = UIO_WRITE`.

A naive handler might call `uiomove(sc->intbuf, 16, uio)`, where `sc->intbuf` is `int sc->intbuf[4];`. `uiomove` would issue a `copyin` that copies the 16 bytes. On success, `sc->intbuf` would hold the four integers in the byte order of the calling program.

But note: the user may have written those integers in the byte order of a completely different CPU if the driver is ever used across architectures. The user may have used `int32_t` where the driver used `int`. The user may have padded the structure differently. For `myfirst` none of this matters because we treat the data as opaque bytes. For a driver that exposes structured data over `read`/`write`, these issues multiply quickly, and they are the reason most real drivers either use `ioctl` for structured payloads or declare an explicit wire format (byte order, field widths, alignment) in their documentation.

The lesson: `uiomove` moves bytes. It does not know or care about types. Your driver has to decide what those bytes mean.

### A Mini Case Study: When a Struct Round-Trip Goes Wrong

To make the "bytes are not types" point concrete, walk through a plausible but mistaken attempt to expose a kernel counter via `read(2)` as a typed structure.

Suppose your driver maintains a set of counters:

```c
struct myfirst_stats {
        uint64_t reads;
        uint64_t writes;
        uint64_t errors;
        uint32_t flags;
};
```

And suppose, optimistically, you expose them via `d_read`:

```c
static int
stats_read(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct myfirst_softc *sc = dev->si_drv1;
        struct myfirst_stats snap;

        mtx_lock(&sc->mtx);
        snap.reads  = sc->stat_reads;
        snap.writes = sc->stat_writes;
        snap.errors = sc->stat_errors;
        snap.flags  = sc->stat_flags;
        mtx_unlock(&sc->mtx);

        return (uiomove(&snap, sizeof(snap), uio));
}
```

At first glance this is fine. The bytes reach user space. A reader can cast the buffer to a `struct myfirst_stats` and look at the fields. The author tests it on amd64, sees the correct values, ships the driver.

Three problems are sitting there waiting.

**Problem 1: Struct padding.** The layout of `struct myfirst_stats` depends on the compiler and architecture. On amd64 with the default ABI, `uint64_t` is 8-byte-aligned, so the struct is 8 bytes for `reads`, 8 for `writes`, 8 for `errors`, 4 for `flags`, plus 4 bytes of trailing padding to round the size to 32. The user program must declare a struct with the *same* padding to read the fields correctly. A user program that redeclares the struct with `#pragma pack(1)` or uses a different compiler version will parse the bytes wrong and see garbage in `errors`.

**Problem 2: Byte order.** An amd64 machine stores `uint64_t` little-endian. A user program running on the same architecture decodes correctly. A user program running remotely on a big-endian machine, reading the bytes through a network pipe, sees the integers byte-swapped. The driver did not choose an on-the-wire byte order, so the format is CPU-dependent by accident.

**Problem 3: Atomicity of the snapshot.** The reader may pull the bytes out through `uiomove` after `mtx_unlock` releases the mutex and before the kernel returns control to the caller. Between those two moments, the fields `snap.reads`, `snap.writes`, etc. are already captured in the stack-local `snap`, so *that* bit is fine. But the example is small enough that the bug does not appear; a larger snapshot might be captured across several mutex acquisitions and exhibit torn reads.

**The fix is not to "try harder" with struct layout.** The fix is to stop using `read(2)` for structured data. Two better options exist:

- **`sysctl`**: the chapter has been using this throughout. Individual counters are exposed as named nodes of known type. `sysctl(3)` on the user side returns integers directly; no struct layout, no padding, no byte order.
- **`d_ioctl`**: Chapter 25 builds out `ioctl` properly. For this use case, an `ioctl` with a well-defined request structure would be appropriate, and the `_IOR` / `_IOW` macros document the size and direction.

The `read(2)` interface promises "a byte stream the driver defines"; nothing more. If you respect the promise, your driver is portable, testable, and resistant to silent layout drift. If you break the promise by exposing typed structures, you inherit every ABI trap that network protocols spent decades learning to work around.

For `myfirst` in this chapter we never hit the problem, because we only ever push and pull opaque byte streams. The point of the case study is to help you recognise the shape of the mistake before someone hands you a driver that is already making it.

### Summary of This Section

- Use `uiomove(9)` inside `d_read` and `d_write`. It reads the uio, picks the right primitive, and handles user / kernel faults for you.
- Use `uiomove_frombuf(9)` when you want automatic offset-into-buffer arithmetic.
- Use `copyin(9)` and `copyout(9)` only when you have a user pointer outside the uio context, typically in `d_ioctl`.
- Do not dereference user pointers directly. Ever.
- Check return values. Any copy can fail with `EFAULT`, and your handler must propagate the error.

These rules are short, but they cover nearly every I/O safety mistake that beginner drivers make.



## Managing Internal Buffers in Your Driver

The read and write handlers are the visible surface of your driver's I/O path. Behind them, the driver has to store data somewhere. This section is about how that storage is designed, allocated, protected, and cleaned up, at the beginner-friendly level that this chapter needs. Chapter 10 will extend the buffer into a true ring and make it concurrent-safe under load; we are staying short of that here on purpose.

### Why You Need Buffers

A buffer is temporary storage between I/O calls. A driver uses one for at least three reasons:

1. **Rate matching.** Producers and consumers do not arrive at the same time. A write can deposit bytes that a later read will pick up.
2. **Request reshaping.** A user may read in units that do not line up with how the driver produces data. A buffer absorbs the mismatch.
3. **Isolation.** The bytes inside the driver's buffer are kernel data. They are not user pointers, not DMA addresses, not in a scatter-gather list. Everything in a kernel buffer is addressable by the driver safely and uniformly.

For `myfirst`, the buffer is a small piece of in-RAM storage. `d_write` writes into it; `d_read` reads from it. The buffer is the driver's state. The uio machinery is the plumbing that moves bytes in and out.

### Static vs Dynamic Allocation

Two reasonable designs exist for where the buffer lives.

**Static allocation** puts the buffer inside the softc structure or as a module-level array:

```c
struct myfirst_softc {
        ...
        char buf[4096];
        size_t bufused;
        ...
};
```

Pros: allocation never fails, sizing is explicit, lifetime is trivially tied to the softc. Cons: the size is baked in at compile time; if you later want to make it tunable, you will refactor.

**Dynamic allocation** uses `malloc(9)` from an `M_*` bucket:

```c
sc->buf = malloc(sc->buflen, M_DEVBUF, M_WAITOK | M_ZERO);
```

Pros: size can be chosen at attach time from a sysctl or tunable; can be resized if you are careful. Cons: allocation can fail (less relevant with `M_WAITOK`, more so with `M_NOWAIT`); the driver owns another free path.

For small buffers, static allocation inside the softc is the simplest choice, and the one Chapter 7 used implicitly by relying on Newbus to allocate the whole softc. Chapter 9 will use dynamic allocation because the buffer is large enough that putting it in the softc is slightly wasteful, and because the dynamic path is the pattern you will use repeatedly later in the book.

### The `malloc(9)` Call

The kernel's `malloc(9)` takes three arguments: the size, a malloc type (which is a tag the kernel uses for accounting and debugging), and a flag word. A common shape:

```c
sc->buf = malloc(sc->buflen, M_DEVBUF, M_WAITOK | M_ZERO);
```

`M_DEVBUF` is the generic "device buffer" malloc type, defined across the tree and appropriate for driver-private data that does not deserve a dedicated type. If your driver grows large enough to warrant its own tag, you can declare one with `MALLOC_DECLARE(M_MYFIRST)` and `MALLOC_DEFINE(M_MYFIRST, "myfirst", "myfirst driver data")`, and use `M_MYFIRST` instead. For now, `M_DEVBUF` is fine.

The flag bits most relevant at this stage:

- `M_WAITOK`: it is OK for the allocation to sleep waiting for memory. In attach context this is almost always the right choice.
- `M_NOWAIT`: do not sleep; return `NULL` if memory is tight. Needed when you are in a context that cannot sleep (interrupt handler, inside a non-sleepable lock).
- `M_ZERO`: zero the memory before returning. Pair with `M_WAITOK` or `M_NOWAIT` as appropriate.

A call with `M_WAITOK` without `M_NOWAIT` is guaranteed to return a valid pointer on FreeBSD 14.3. The kernel will sleep and possibly trigger reclamation if needed, but it will not return `NULL` in practice. Still, checking for `NULL` is defensive and costs nothing; we will do it.

### The Matching `free(9)` Call

Every `malloc(9)` has a matching `free(9)`. The signature is:

```c
free(sc->buf, M_DEVBUF);
```

The malloc type passed to `free` must match the one passed to `malloc` for the same pointer. Passing a different type would corrupt the kernel's accounting and is one of the bugs `INVARIANTS`-enabled kernels catch at runtime.

Where to put the `free` depends on where the `malloc` was: attach allocates, detach frees. If attach fails partway, the error-unwind path frees whatever was allocated before the failure. We saw this pattern in Chapter 7; we will reuse it here.

### Buffer Sizing

Picking a buffer size is a design choice. For a classroom driver, any small size works. A few guidelines:

- **Small** (a few hundred bytes to a few kilobytes): fine for demonstration. Easy to reason about. User workloads larger than the buffer will observe `ENOSPC` or short reads quickly; this is a feature for teaching, not a bug.
- **Page-sized** (4096 bytes): a common, sensible default. Memory allocation is page-aligned for free, and many tools treat 4 KiB as a natural unit.
- **Larger** (many kilobytes to a megabyte): appropriate for drivers that expect to buffer a lot of data. Remember that kernel memory is not infinite; a runaway driver that allocates a megabyte per open can destabilise the system.

For `myfirst` Stage 2 we will use a 4096-byte buffer. It is large enough that a reasonable test (a paragraph of text, a few integers) fits, and small enough that `ENOSPC` behaviour is easy to trigger from a shell.

### Buffer Overflows

The single most common bug in a driver that manages its own buffer is writing past the end of the buffer. The bug is absolutely fatal in kernel space. A user-space program that overruns a buffer might corrupt its own heap; a kernel module that does the same may corrupt another subsystem's memory, and the crash (or worse, the silent misbehaviour) can show up far from the bug.

The defence is arithmetic discipline. Every time your code is about to write `N` bytes starting at offset `O` into a buffer of size `S`, verify that `O + N <= S` before the write. In the Stage 3 handler above, the expression `towrite = MIN((size_t)uio->uio_resid, avail)` is exactly that check: `towrite` is clamped to `avail`, where `avail` is `sc->buflen - sc->bufused`. No way to exceed `sc->buflen`.

A related bug is signed-vs-unsigned confusion. `uio_resid` is `ssize_t`; `sc->bufused` is `size_t`. Mixing them carelessly can produce a negative value that wraps around when cast to `size_t`, with catastrophic results. The `MIN` macro and an explicit `(size_t)` cast are worth the tiny amount of noise they add to the code.

### Locking Considerations

If your driver can be entered from more than one user context simultaneously, the buffer needs a lock. Two simultaneous writers could race on `bufused`; two simultaneous readers could race on read offsets; a writer and a reader could interleave their state updates in a way that corrupts both.

In `myfirst`, the `struct mtx mtx` field we have carried since Chapter 7 is the lock we will use. It is an ordinary `MTX_DEF` mutex, which means it can be held across a `uiomove` call (which may sleep on a page fault). We will hold it during every update to `bufused` and during the `uiomove` that is transferring bytes into or out of the shared buffer.

Part 3 goes much deeper into locking strategy. For now, the rule is: **protect any field that more than one handler might touch at the same time**. In Stage 3, that is `sc->buf` and `sc->bufused`. Your per-open `fh` is per-descriptor; it does not need the same lock, because two handlers cannot run for the same descriptor concurrently in the cases we will exercise.

### A Preview of Circular Buffers

Chapter 10 builds a proper ring buffer: a fixed-size buffer where `head` and `tail` pointers chase each other around. It differs from the linear buffer we are using in Chapter 9 in two ways:

1. It does not need to be reset between uses. The pointers wrap; the buffer is reused in place.
2. It can support streaming at steady state. A linear buffer fills up and then refuses writes; a ring buffer maintains a moving window of recent data.

Stage 3 of this chapter does *not* implement a ring. It implements a linear buffer that `d_write` appends into and `d_read` drains from. When the buffer is full, `d_write` returns `ENOSPC`; when it is empty, `d_read` returns zero bytes. This is enough to get the I/O path right without the extra bookkeeping of a ring. Chapter 10 adds that bookkeeping on top of the same handler shapes.

### A Note on Thread Safety of the Per-Descriptor fh

The `struct myfirst_fh` your driver allocates in `d_open` is per-descriptor. Two handlers for the same descriptor cannot execute concurrently in the scenarios this chapter exercises (the kernel serialises per-file operations through the file-descriptor machinery for the common cases), so the fields inside `fh` do not need their own lock. Two handlers for *different* descriptors do run concurrently, but they touch different `fh` structures.

This is a comforting invariant but not an absolute one. A driver that arranges to pass the `fh` pointer to a kernel thread that runs in parallel with the syscall must add its own synchronisation. We will not do that in this chapter; for now the `fh` is safe as long as you only touch it from inside the handler that was given the descriptor.

### Kernel Helpers You Should Recognise

Before we move on, it is worth naming the small handful of helper macros and functions the chapter has been using. They are defined in standard FreeBSD headers, and beginners sometimes copy-paste code that uses them without knowing where they come from or what constraints apply.

`MIN(a, b)` and `MAX(a, b)` are available in kernel code through `<sys/libkern.h>`, which is pulled in transitively by `<sys/systm.h>`. They evaluate each argument at most twice, so a `MIN(count++, limit)` is a bug: `count` increments twice. A well-written driver avoids side effects inside `MIN`/`MAX` arguments.

```c
towrite = MIN((size_t)uio->uio_resid, avail);
```

The explicit `(size_t)` cast is part of the pattern, not a stylistic flourish. `uio_resid` is `ssize_t`, which is signed; `avail` is `size_t`, which is unsigned. Without the cast, the compiler picks one type for the comparison, and modern compilers warn when signed and unsigned meet in the same `MIN` / `MAX`. The cast makes the intent explicit: we have already checked that `uio_resid` is non-negative (the kernel guarantees it), so the cast is safe.

`howmany(x, d)`, defined in `<sys/param.h>`, computes `(x + d - 1) / d`. Use it when you need ceiling division. A driver that allocates pages to hold a byte-count often writes:

```c
npages = howmany(buflen, PAGE_SIZE);
```

`rounddown(x, y)` and `roundup(x, y)` align `x` down or up to the nearest multiple of `y`. `roundup2` and `rounddown2` are faster variants that only work when `y` is a power of two. These are how drivers page-align buffers or block-align offsets.

`__DECONST(type, ptr)` casts away `const` without a compiler warning. It is the polite way to tell the compiler "I know this pointer is declared `const`, but I have verified that the function I am calling will not modify the data, so please stop complaining". Used it around `zero_region` in `null.c`'s `zero_read`; we used it in Stage 1's `myfirst_read`. Prefer it to a plain `(void *)` cast, because it signals intent.

`curthread` is an architecture-specific macro (resolved through a per-CPU register) that points at the currently executing thread. `uio->uio_td` usually equals `curthread` when the uio came from a syscall; the two are interchangeable in that context but the uio-carried value is more self-documenting.

`bootverbose` is an integer that is set to non-zero if the kernel was booted with `-v` or if the operator toggled it via sysctl. Guarding chatty log lines with `if (bootverbose)` is the FreeBSD idiom for debug logging that is visible on demand but silent by default.

Recognising these helpers when you encounter them in other drivers shortens the time it takes to read unfamiliar code. None of them are exotic; all of them are part of the standard vocabulary a kernel contributor is expected to read without looking up.



## Error Handling and Edge Cases

A beginner driver that "works on the happy path" is a driver that will eventually crash the kernel. The interesting part of I/O handling is the parts that are not the happy path: zero-length reads, partial writes, bad user pointers, signals delivered mid-call, exhausted buffers, and several dozen variations of those. This section walks through the common cases and the errno values that go with them.

### The Errno Values That Matter for I/O

FreeBSD has a large errno space. Only a handful are frequent in driver I/O paths; learning them well is more useful than skimming the whole list.

`0`: success. Return this when the transfer completed cleanly. The byte count is implicit in `uio_resid`.

`ENXIO` ("Device not configured"): the operation cannot proceed because the device is not in a usable state. Return this from `d_open`, `d_read`, or `d_write` if the softc is missing, `is_attached` is false, or the driver has been told to shut down. It is the idiomatic "the cdev exists but the backing device does not" error.

`EFAULT` ("Bad address"): a user pointer was invalid. You rarely return this directly; `uiomove(9)` returns it on your behalf when a `copyin`/`copyout` fails. Propagate it by returning whatever error `uiomove` produced.

`EINVAL` ("Invalid argument"): some parameter is nonsensical. For a read or write, this is usually an out-of-range offset (if your driver honours offsets) or a malformed request. Avoid using it as a catch-all.

`EAGAIN` ("Resource temporarily unavailable"): the operation would block, but `O_NONBLOCK` was set. For a `d_read` that has no data, this is the right answer in non-blocking mode. For a `d_write` that has no space, same story. We will handle this in Stage 3.

`EINTR` ("Interrupted system call"): a signal was delivered while the thread was blocked inside the driver. Your `d_read` may return `EINTR` if a sleep was interrupted by a signal. The kernel will then either retry the syscall transparently (depending on the `SA_RESTART` flag) or return to userland with `errno = EINTR`. We will see `EINTR` handling in Chapter 10; Chapter 9 does not block and so does not produce `EINTR`.

`EIO` ("Input/output error"): a catch-all for hardware errors. Use when your driver talks to real hardware and the hardware reports a failure. Rare in `myfirst`, which has no hardware.

`ENOSPC` ("No space left on device"): the driver's buffer is full and cannot accept more data. The correct response for a write when there is no room. Stage 3 returns this.

`EPIPE` ("Broken pipe"): used by pipe-like drivers when the peer has closed. Not relevant for `myfirst`.

`ERANGE`, `EOVERFLOW`, `EMSGSIZE`: less common in character drivers; they show up when the kernel or the driver wants to say "the number you asked for is out of range". We will not use them in this chapter.

### End of File in a Read

By convention, a read that returns zero bytes (because `uiomove` did not move anything and your handler returned zero) is interpreted by the caller as end of file. Shells, `cat`, `head`, `tail`, `dd`, and most other base-system tools rely on this convention.

The implication for your `d_read`: when your driver has nothing more to deliver, return zero. Do not return an errno. `uio_resid` should still have its original value, because no bytes were moved.

In Stage 1 and Stage 2 of `myfirst`, EOF happens when the per-descriptor read offset has reached the buffer's length. `uiomove_frombuf` returns zero in that case naturally, so we do not need a special code path.

In Stage 3, where `d_read` drains a buffer that `d_write` has appended to, EOF behaviour is subtler: "no data right now" is not the same as "no more data ever". We will report "no data right now" as a zero-byte read. A thoughtful user program might interpret that as EOF and stop; a less thoughtful one will loop and call `read(2)` again. Chapter 10 introduces the proper blocking-read or `poll(2)` strategies that allow a user program to wait for more data without spinning.

### Zero-Length Reads and Writes

A zero-length request (`read(fd, buf, 0)` or `write(fd, buf, 0)`) is legal. It means "do nothing, but tell me whether you could have done something". The kernel handles most of the dispatch for you: if `uio_resid` is zero at entry, any `uiomove` call is a no-op, and your handler returns zero. The caller sees a zero-byte transfer and no error.

Two subtle points. First, do not treat `uio_resid == 0` as an error condition. It is not. It is a legitimate request. Second, do not assume `uio_resid == 0` means end of file; it just means the caller asked for zero bytes. EOF is about the driver running out of data, not about the caller asking for none.

### Short Transfers

A short read is a read that returns fewer bytes than were requested. A short write is a write that consumed fewer bytes than were offered. Both are legal and expected in UNIX I/O; well-written user programs handle them by looping.

Your driver is the authoritative decider of how much to transfer. The `uiomove` family of functions transfers at most `MIN(user_request, driver_offer)` bytes in a call. If your code calls `uiomove(buf, 128, uio)` and the user asked for 1024, the kernel transfers 128 and leaves 896 in `uio_resid`. The caller sees a 128-byte return from `read(2)`.

A badly-behaved user program that does not loop on short I/O will miss bytes. That is not your driver's problem; UNIX has been like this since 1971. A well-behaved driver is one that returns honest byte counts (through `uio_resid`) and predictable errno values, even when partial transfers happen.

### Handling EFAULT From uiomove

When `uiomove(9)` returns a non-zero error, the most common value is `EFAULT`. By the time you see it, the kernel has already:

- Installed a fault handler around the copy.
- Observed the fault.
- Unwound the partial copy.
- Returned `EFAULT` to `uiomove`'s caller.

Your handler has two options for how to respond:

1. **Propagate the error**. Return `EFAULT` (or whatever errno was returned) from `d_read` / `d_write`. This is the simplest and almost always correct.
2. **Adjust driver state and return success**. If some bytes moved before the fault, `uio_resid` may have already decreased. The kernel will report that partial success to user space. You may want to update any driver-side counter that reflects how far the transfer got.

In practice, option 1 is the universal answer unless you have a specific reason to do more. Option 2 adds complexity that is rarely worth it.

### Defensive Programming for User Input

Every byte a user writes into your device is untrusted. That sounds dramatic; it is also literally true. A kernel module that parses a user write as a structure and dereferences a pointer from that structure is a kernel module with a trivial arbitrary-memory-write vulnerability.

The rule of thumb: **treat the bytes in your buffer as arbitrary data, not as a typed structure, unless you have deliberately chosen a wire format that you validate at every boundary**. For `myfirst` this is easy, because we never interpret the bytes; they are payload. For drivers that expose a structured write interface (for example, a driver that lets users configure behaviour through writes), the defensive path is to:

- Validate the length of the write against the expected message size.
- Copy the bytes into a kernel-space structure (not a user pointer).
- Validate every field of that structure before acting on it.
- Never store a user pointer inside your driver for later use.

These rules are easier to follow than they sound, but they are easy to break without noticing. Chapter 25 revisits them when we look at `ioctl` design. For now, the bar is low: your `myfirst` driver should copy bytes through `uiomove`, not interpret them.

### Logging Errors vs Silent Failures

When a handler returns an errno, the error propagates to user space as the `errno` value of the failed syscall. Most user programs will see it there and report it. Some will swallow it.

For driver development, it helps to also log significant errors to `dmesg`. `device_printf(9)` is the right tool, because it tags each line with the Newbus device name so you can tell which instance produced the message. An example from Stage 3:

```c
if (avail == 0) {
        mtx_unlock(&sc->mtx);
        if (bootverbose)
                device_printf(sc->dev, "write rejected: buffer full\n");
        return (ENOSPC);
}
```

The `if (bootverbose)` guard is a common FreeBSD idiom for chatty logging: it only prints if the kernel was booted with the `-v` flag or if the `bootverbose` sysctl was set, which keeps production logs quiet while still giving developers a way to see detail.

Do not log every error on every call; that produces log spam, which makes real problems harder to find. Log the first occurrence of a condition, or log periodically, or log only under `bootverbose`. The choice depends on the driver. For `myfirst`, a single log per transition (buffer-empty, buffer-full) is plenty.

### Predictability and User-Friendliness

A beginner writing a driver often focuses on making the good path fast. A more experienced driver author focuses on making the error paths predictable. The difference is this: when an operator runs your driver and something breaks, the errno value, the log message, and the user-space reaction need to add up to a clear story. If `read(2)` returns `-1` with `errno = EIO` and the logs are silent, the operator has nowhere to start. If the logs say "myfirst0: read failed, device detached" and the user gets `ENXIO`, the story tells itself.

Aim for that. Return the right errno. Log the underlying reason once. Make partial transfers honest. Never silently discard data.

### A Short Table of Conventions

| Situation in d_read                             | Return        |
|-------------------------------------------------|---------------|
| No data to deliver, more might arrive later     | `0` with `uio_resid` unchanged |
| No data, never will be any more (EOF)           | `0` with `uio_resid` unchanged |
| Some data delivered, some not                   | `0` with `uio_resid` reflecting remainder |
| Full delivery                                   | `0` with `uio_resid = 0` |
| User pointer invalid                            | `EFAULT` (from `uiomove`) |
| Device not ready / detaching                    | `ENXIO` |
| Non-blocking, would block                       | `EAGAIN` |
| Hardware error                                  | `EIO` |

| Situation in d_write                            | Return        |
|-------------------------------------------------|---------------|
| Full acceptance                                 | `0` with `uio_resid = 0` |
| Partial acceptance                              | `0` with `uio_resid` reflecting remainder |
| No room, would block                            | `EAGAIN` (non-blocking) or sleep (blocking) |
| No room, permanent                              | `ENOSPC` |
| Invalid pointer                                 | `EFAULT` (from `uiomove`) |
| Device not ready                                | `ENXIO` |
| Hardware error                                  | `EIO` |

Both tables are short on purpose. Most drivers use only four or five errno values in total. The cleaner your error story, the better your driver is to work with.



## Evolving Your Driver: The Three Stages

With the theory in place, we turn to code. This section walks through three stages of `myfirst`, each one small, each one a complete driver that loads, runs, and exercises a specific I/O pattern.

The stages are designed to build on one another:

- **Stage 1** adds a read path that serves a fixed kernel-space message. This is `myfirst_read` at its simplest.
- **Stage 2** adds a write path that deposits user data into a kernel buffer, and a read path that reads from the same buffer. The buffer is sized at attach time and does not wrap.
- **Stage 3** turns Stage 2 into a first-in / first-out buffer, so that writes append and reads drain, and the driver can serve a continuous (if finite) stream.

All three stages start from the Chapter 8 stage 2 source. The build system (`Makefile`) does not change. The `attach` and `detach` handlers grow slightly at each stage. The shape of `cdevsw`, the Newbus methods, the per-open `fh` plumbing, and the sysctl tree stay the same. You will spend most of your time looking at `d_read` and `d_write`.

### Stage 1: A Static-Message Reader

The Stage 1 driver holds a fixed message in kernel memory and serves it to readers. `d_read` uses `uiomove_frombuf(9)` to deliver the message. `d_write` stays a stub: it returns success without consuming any bytes. This stage is the bridge from the Chapter 8 stub to a real reader; it introduces `uiomove_frombuf` in the smallest possible context.

Add a pair of fields to the softc to hold the message and its length, and a per-descriptor offset to the `fh`:

```c
struct myfirst_softc {
        /* ...existing Chapter 8 fields... */

        const char *message;
        size_t      message_len;
};

struct myfirst_fh {
        struct myfirst_softc *sc;
        uint64_t              reads;
        uint64_t              writes;
        off_t                 read_off;
};
```

In `myfirst_attach`, initialise the message:

```c
static const char myfirst_message[] =
    "Hello from myfirst.\n"
    "This is your first real read path.\n"
    "Chapter 9, Stage 1.\n";

sc->message = myfirst_message;
sc->message_len = sizeof(myfirst_message) - 1;
```

Note the `- 1`: we do not want to serve the terminating NUL byte to user space. Text files do not carry a NUL at the end, and neither should a device that behaves like one.

The new `myfirst_read`:

```c
static int
myfirst_read(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct myfirst_softc *sc = dev->si_drv1;
        struct myfirst_fh *fh;
        off_t before;
        int error;

        error = devfs_get_cdevpriv((void **)&fh);
        if (error != 0)
                return (error);

        if (sc == NULL || !sc->is_attached)
                return (ENXIO);

        before = uio->uio_offset;
        error = uiomove_frombuf(__DECONST(void *, sc->message),
            sc->message_len, uio);
        if (error == 0)
                fh->reads += (uio->uio_offset - before);
        fh->read_off = uio->uio_offset;
        return (error);
}
```

Two details are worth stopping on.

First, `uio->uio_offset` is the per-descriptor position in the stream. The kernel maintains it across calls, advancing it as `uiomove_frombuf` moves bytes. The first `read(2)` on a freshly opened descriptor starts at offset zero; each subsequent `read(2)` starts where the previous one ended. When the offset reaches `sc->message_len`, `uiomove_frombuf` returns zero without moving any bytes, and the caller sees EOF.

Second, `before` captures `uio->uio_offset` at entry so we can compute how many bytes moved. After `uiomove_frombuf` returns, the difference is the transfer size, and we add it to the per-descriptor `reads` counter. This is where the `fh->reads` field from Chapter 8 finally earns its keep.

The `__DECONST` cast is a FreeBSD idiom for casting away `const`. `uiomove_frombuf` takes a non-`const` `void *` because it is prepared to move in either direction, but in this context we know the direction is kernel-to-user (a read), so we know the kernel buffer will not be modified. Stripping the `const` here is safe; using a plain `(void *)` cast would work as well but is less self-documenting.

`myfirst_write` stays as Chapter 8 left it for Stage 1:

```c
static int
myfirst_write(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct myfirst_fh *fh;
        int error;

        error = devfs_get_cdevpriv((void **)&fh);
        if (error != 0)
                return (error);

        (void)fh;
        uio->uio_resid = 0;
        return (0);
}
```

Writes are accepted and discarded, the `/dev/null` shape. Stage 2 will change this.

Build and load. A quick smoke test from userland:

```sh
% cat /dev/myfirst/0
Hello from myfirst.
This is your first real read path.
Chapter 9, Stage 1.
%
```

A second read from the same descriptor returns EOF, because the offset is already past the end of the message:

```sh
% cat /dev/myfirst/0 /dev/myfirst/0
Hello from myfirst.
This is your first real read path.
Chapter 9, Stage 1.
Hello from myfirst.
This is your first real read path.
Chapter 9, Stage 1.
```

Wait: `cat` reads the message twice. That is because `cat` opens the file twice (once per argument) and each open gets a fresh descriptor with its own `uio_offset`. If you want to verify that two opens really do see independent offsets, open the device from a small C program and read more than once from the same descriptor:

```c
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>

int
main(void)
{
        int fd = open("/dev/myfirst/0", O_RDONLY);
        if (fd < 0) { perror("open"); return 1; }
        char buf[64];
        ssize_t n;
        while ((n = read(fd, buf, sizeof(buf))) > 0) {
                fwrite(buf, 1, n, stdout);
        }
        close(fd);
        return 0;
}
```

The first `read(2)` returns the message; the second returns zero (EOF); the program exits. This confirms that `uio_offset` is being maintained per descriptor.

Stage 1 is intentionally short. It introduces three ideas (the `uiomove_frombuf` helper, per-descriptor offsets, the `__DECONST` idiom) without overloading the reader. The rest of the chapter builds on this.

### Stage 2: A Write-Once / Read-Many Buffer

Stage 2 extends the driver to accept writes. The driver allocates a kernel buffer at attach, writes deposit into it, reads deliver from it. There is no wraparound: once the buffer fills, further writes return `ENOSPC`. Reads see whatever has been written so far, starting from their own per-descriptor offset.

The shape of `myfirst_softc` grows by a few fields:

```c
struct myfirst_softc {
        /* ...existing Chapter 8 fields... */

        char    *buf;
        size_t   buflen;
        size_t   bufused;

        uint64_t bytes_read;
        uint64_t bytes_written;
};
```

`buf` is the pointer returned by `malloc(9)`. `buflen` is its size, a compile-time constant for simplicity; you can make it tunable later. `bufused` is the high-water mark: the number of bytes that have been written so far.

Two new sysctl nodes for observability:

```c
SYSCTL_ADD_U64(&sc->sysctl_ctx, SYSCTL_CHILDREN(sc->sysctl_tree),
    OID_AUTO, "bytes_written", CTLFLAG_RD,
    &sc->bytes_written, 0, "Total bytes written into the buffer");

SYSCTL_ADD_UINT(&sc->sysctl_ctx, SYSCTL_CHILDREN(sc->sysctl_tree),
    OID_AUTO, "bufused", CTLFLAG_RD,
    &sc->bufused, 0, "Current byte count in the buffer");
```

`bufused` is a `size_t`, and the sysctl macro for unsigned integer is `SYSCTL_ADD_UINT` on 32-bit platforms or `SYSCTL_ADD_U64` on 64-bit platforms. Since this driver targets FreeBSD 14.3 on amd64 in the typical lab, `SYSCTL_ADD_UINT` is fine; the field will be presented as an `unsigned int` even though the internal type is `size_t`. If you target arm64 or another 64-bit platform, use `SYSCTL_ADD_U64` and cast accordingly.

Allocate the buffer in `attach`:

```c
#define MYFIRST_BUFSIZE 4096

sc->buflen = MYFIRST_BUFSIZE;
sc->buf = malloc(sc->buflen, M_DEVBUF, M_WAITOK | M_ZERO);
if (sc->buf == NULL) {
        error = ENOMEM;
        goto fail_mtx;
}
sc->bufused = 0;
```

Free it in `detach`:

```c
if (sc->buf != NULL) {
        free(sc->buf, M_DEVBUF);
        sc->buf = NULL;
}
```

Adjust the error-unwind in `attach` to include the buffer free:

```c
fail_dev:
        if (sc->cdev_alias != NULL) {
                destroy_dev(sc->cdev_alias);
                sc->cdev_alias = NULL;
        }
        destroy_dev(sc->cdev);
        sysctl_ctx_free(&sc->sysctl_ctx);
        free(sc->buf, M_DEVBUF);
        sc->buf = NULL;
fail_mtx:
        mtx_destroy(&sc->mtx);
        sc->is_attached = 0;
        return (error);
```

Now the read handler:

```c
static int
myfirst_read(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct myfirst_softc *sc = dev->si_drv1;
        struct myfirst_fh *fh;
        off_t before;
        size_t have;
        int error;

        error = devfs_get_cdevpriv((void **)&fh);
        if (error != 0)
                return (error);
        if (sc == NULL || !sc->is_attached)
                return (ENXIO);

        mtx_lock(&sc->mtx);
        have = sc->bufused;
        before = uio->uio_offset;
        error = uiomove_frombuf(sc->buf, have, uio);
        if (error == 0) {
                sc->bytes_read += (uio->uio_offset - before);
                fh->reads += (uio->uio_offset - before);
        }
        fh->read_off = uio->uio_offset;
        mtx_unlock(&sc->mtx);
        return (error);
}
```

The read handler takes the mutex to read `bufused` consistently, then calls `uiomove_frombuf` with the current high-water mark as the effective buffer size. A reader that runs before any writes happen will see `have = 0` and `uiomove_frombuf` will return zero, which the caller interprets as EOF. A reader that runs after some writes will see the current `bufused` and receive up to that many bytes.

The write handler:

```c
static int
myfirst_write(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct myfirst_softc *sc = dev->si_drv1;
        struct myfirst_fh *fh;
        size_t avail, towrite;
        int error;

        error = devfs_get_cdevpriv((void **)&fh);
        if (error != 0)
                return (error);
        if (sc == NULL || !sc->is_attached)
                return (ENXIO);

        mtx_lock(&sc->mtx);
        avail = sc->buflen - sc->bufused;
        if (avail == 0) {
                mtx_unlock(&sc->mtx);
                return (ENOSPC);
        }
        towrite = MIN((size_t)uio->uio_resid, avail);
        error = uiomove(sc->buf + sc->bufused, towrite, uio);
        if (error == 0) {
                sc->bufused += towrite;
                sc->bytes_written += towrite;
                fh->writes += towrite;
        }
        mtx_unlock(&sc->mtx);
        return (error);
}
```

Notice the clamp: `towrite = MIN(uio->uio_resid, avail)`. If the user asked to write 8 KiB and we have 512 bytes of room, we accept 512 bytes and let the kernel report a short write of 512 back to user space. A well-behaved caller will loop with the remaining bytes; a less well-behaved caller will lose the surplus. That is the caller's responsibility; the driver has done its part honestly.

Smoke-test from userland:

```sh
% sudo kldload ./myfirst.ko
% echo "hello" | sudo tee /dev/myfirst/0 > /dev/null
% cat /dev/myfirst/0
hello
% echo "more" | sudo tee -a /dev/myfirst/0 > /dev/null
% cat /dev/myfirst/0
hello
more
% sysctl dev.myfirst.0.stats.bufused
dev.myfirst.0.stats.bufused: 11
%
```

The buffer grew by 6 bytes for `"hello\n"`, then by 5 more for `"more\n"`, yielding 11 bytes. `cat` reads all 11 bytes back. A second `cat` from a fresh open starts at offset zero and reads them again.

What happens if we write more than the buffer can hold?

```sh
% dd if=/dev/zero bs=1024 count=8 | sudo tee /dev/myfirst/0 > /dev/null
dd: stdout: No space left on device
tee: /dev/myfirst/0: No space left on device
8+0 records in
7+0 records out
```

`dd` wrote 7 blocks of 1024 bytes before the 8th one failed. `tee` reports the error. The driver accepted up to its limit and then returned `ENOSPC` cleanly. The kernel carried the errno value back to user space.

### Stage 3: A First-In / First-Out Echo Driver

Stage 3 turns the buffer into a FIFO. Writes append to the tail. Reads drain from the head. When the buffer is empty, reads return zero bytes (EOF-on-empty). When the buffer is full, writes return `ENOSPC`.

The buffer remains linear: no wrap-around. After a read that drains all the data, `bufused` is zero, and the next write starts again at offset zero in `sc->buf`. This keeps the bookkeeping minimal and focuses the stage on the I/O direction change rather than ring-buffer mechanics.

The softc gains one more field:

```c
struct myfirst_softc {
        /* ...existing fields... */

        size_t  bufhead;   /* index of next byte to read */
        size_t  bufused;   /* bytes in the buffer, from bufhead onward */

        /* ...remaining fields... */
};
```

`bufhead` is the offset of the first byte still to be read. `bufused` is the number of valid bytes starting at `bufhead`. The invariant `bufhead + bufused <= buflen` always holds.

Reset both in `attach`:

```c
sc->bufhead = 0;
sc->bufused = 0;
```

New read handler:

```c
static int
myfirst_read(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct myfirst_softc *sc = dev->si_drv1;
        struct myfirst_fh *fh;
        size_t toread;
        int error;

        error = devfs_get_cdevpriv((void **)&fh);
        if (error != 0)
                return (error);
        if (sc == NULL || !sc->is_attached)
                return (ENXIO);

        mtx_lock(&sc->mtx);
        if (sc->bufused == 0) {
                mtx_unlock(&sc->mtx);
                return (0); /* EOF-on-empty */
        }
        toread = MIN((size_t)uio->uio_resid, sc->bufused);
        error = uiomove(sc->buf + sc->bufhead, toread, uio);
        if (error == 0) {
                sc->bufhead += toread;
                sc->bufused -= toread;
                sc->bytes_read += toread;
                fh->reads += toread;
                if (sc->bufused == 0)
                        sc->bufhead = 0;
        }
        mtx_unlock(&sc->mtx);
        return (error);
}
```

A few details differ from Stage 2. The read no longer honours `uio->uio_offset`; the per-descriptor offset is meaningless for a FIFO where every descriptor sees the same stream and the stream disappears as it is consumed. When `bufused` reaches zero, we reset `bufhead` to zero, which keeps the next write aligned at the beginning of the buffer and avoids pushing data toward the end.

This "collapse on empty" trick is not a ring buffer, but it is close enough for a pedagogical FIFO. The extra re-align step is `O(1)`; it costs almost nothing.

New write handler (mostly unchanged from Stage 2, but note where it appends):

```c
static int
myfirst_write(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct myfirst_softc *sc = dev->si_drv1;
        struct myfirst_fh *fh;
        size_t avail, tail, towrite;
        int error;

        error = devfs_get_cdevpriv((void **)&fh);
        if (error != 0)
                return (error);
        if (sc == NULL || !sc->is_attached)
                return (ENXIO);

        mtx_lock(&sc->mtx);
        tail = sc->bufhead + sc->bufused;
        avail = sc->buflen - tail;
        if (avail == 0) {
                mtx_unlock(&sc->mtx);
                return (ENOSPC);
        }
        towrite = MIN((size_t)uio->uio_resid, avail);
        error = uiomove(sc->buf + tail, towrite, uio);
        if (error == 0) {
                sc->bufused += towrite;
                sc->bytes_written += towrite;
                fh->writes += towrite;
        }
        mtx_unlock(&sc->mtx);
        return (error);
}
```

The write appends at `sc->bufhead + sc->bufused`, not at `sc->bufused` alone, because the valid data slice has moved as reads drained it.

Smoke-test:

```sh
% echo "one" | sudo tee /dev/myfirst/0 > /dev/null
% echo "two" | sudo tee -a /dev/myfirst/0 > /dev/null
% cat /dev/myfirst/0
one
two
% cat /dev/myfirst/0
%
```

After the first `cat`, the buffer is empty. The second `cat` sees no data and exits immediately.

This is the Stage 3 shape. The driver is a small, honest, in-memory FIFO. Users can push bytes in, pull them out, and observe the counters from sysctl. That is real I/O, and it is the waypoint Chapter 10 builds from.



## Tracing a `read(2)` From User Space to Your Handler

Before you start working through the labs, take a step-by-step look at exactly what happens when a user program calls `read(2)` on one of your nodes. Understanding this path is one of those things that changes how you read driver code. Every handler you see in the tree is sitting at the bottom of the call chain described below; once you recognise the chain, every handler starts to look familiar.

### Step 1: The User Program Calls `read(2)`

The C library's `read` wrapper is a thin translation of the call into a system-call trap: it places the file descriptor, the buffer pointer, and the count into the appropriate registers and executes the trap instruction for the current architecture. Control transfers to the kernel.

This part has nothing to do with drivers. It is the same for every syscall. What matters is that the kernel is now executing on behalf of the user process, in the kernel's address space, with the user's registers saved and the process's credentials visible through `curthread->td_ucred`.

### Step 2: The Kernel Looks Up the File Descriptor

The kernel calls `sys_read(2)` (in `/usr/src/sys/kern/sys_generic.c`), which validates the arguments, looks up the file descriptor in the calling process's file table, and acquires a reference on the resulting `struct file`.

If the descriptor is not open, the call fails here with `EBADF`. If the descriptor is open but is not readable (for instance, the user opened the device with `O_WRONLY`), the call fails with `EBADF` as well. The driver is not involved; `sys_read` enforces the access mode.

### Step 3: The Generic File-Operation Vector Dispatches

The `struct file` has a file-type tag (`f_type`) and a file-operations vector (`f_ops`). For a regular file the vector dispatches to the VFS layer; for a socket it dispatches to sockets; for a device opened through devfs, it dispatches to `vn_read`, which in turn calls the vnode operation `VOP_READ` on the vnode behind the file.

This may sound like indirection for its own sake. It is actually how the kernel keeps the rest of the syscall path identical for every kind of file. Drivers do not need to know about this layer; devfs and VFS hand the call to your handler eventually.

### Step 4: VFS Calls Into devfs

The vnode's filesystem ops point to devfs's implementation of the vnode interface (`devfs_vnops`). `VOP_READ` on a devfs vnode calls `devfs_read_f`, which looks at the cdev behind the vnode, acquires a thread-count reference on it (incrementing `si_threadcount`), and calls `cdevsw->d_read`. That is your function.

Two details from this step carry implications for your driver.

First, **the `si_threadcount` increment is what `destroy_dev(9)` uses to know your handler is active**. When a module unloads and `destroy_dev` runs, it waits until every current invocation of every handler returns. The reference is incremented before your `d_read` is called and released after it returns. The mechanism is why your driver can be safely unloaded while a user is in the middle of `read(2)`.

Second, **the call is synchronous from the VFS layer's point of view**. VFS calls your handler, waits for it to return, and then propagates the result. You do not need to do anything special to participate in this synchronisation; just return from your handler when you are done.

### Step 5: Your `d_read` Handler Runs

This is where we have been all chapter. The handler:

- Receives a `struct cdev *dev` (the node being read), a `struct uio *uio` (the I/O description), and an `int ioflag` (flags from the file-table entry).
- Retrieves per-open state via `devfs_get_cdevpriv(9)`.
- Verifies liveness.
- Transfers bytes through `uiomove(9)`.
- Returns zero or an errno.

Nothing about this step should be mysterious by now.

### Step 6: The Kernel Unwinds and Reports

`devfs_read_f` sees your return value. If zero, it computes the byte count from the decrease in `uio->uio_resid` and returns that count. If non-zero, it converts the errno into the syscall's error return. VFS's `vn_read` passes the result upward to `sys_read`. `sys_read` writes the result into the return-value register.

Control transfers back to user space. The C library's `read` wrapper examines the result: a positive value is returned as the return value of `read(2)`; a negative value sets `errno` and returns `-1`.

The user program sees the integer it expected, and its control flow continues.

### Step 7: The Reference Counts Unwind

On the way out, `devfs_read_f` releases the thread-count reference on the cdev. If `destroy_dev(9)` had been waiting for `si_threadcount` to reach zero, it may now proceed with the tear-down.

This is why the whole chain is structured as carefully as it is. Every reference is paired; every increment has a matching decrement; every piece of state the handler touches is either owned by the handler, owned by the softc, or owned by the per-open `fh`. If any of those invariants breaks, unload becomes unsafe.

### Why This Trace Matters to You

Three takeaways.

**The first**: the mechanism above is why your handler does not need to do anything exotic to coexist with module unload. Provided you return from `d_read` in finite time, the kernel will let your driver unload cleanly. This is part of why Chapter 9 keeps all reads non-blocking at the driver level.

**The second**: every layer between `read(2)` and your handler is set up by the kernel before your code runs. The user's buffer is valid (or `uiomove` will report `EFAULT`), the cdev is alive (or devfs would have refused the call), the access mode is compatible with the descriptor (or `sys_read` would have refused), and the process's credentials are the current thread's. You can focus on your driver's job and trust the layers.

**The third**: when you read an unfamiliar driver in the tree and its `d_read` looks weird, you can walk the chain in reverse. Who called this handler? What state did they prepare? What invariants does my handler promise on return? The chain tells you. The answers are usually the same as they are for `myfirst`.

### The Mirror: Tracing a `write(2)`

A write follows the same kind of chain, mirrored. A full seven-step breakdown would be mostly a restatement of the read trace with words substituted, so the paragraph below is deliberately compressed.

The user calls `write(fd, buf, 1024)`. The C library traps into the kernel. `sys_write(2)` in `/usr/src/sys/kern/sys_generic.c` validates arguments, looks up the descriptor, and acquires a reference on its `struct file`. The file-ops vector dispatches to `vn_write`, which calls `VOP_WRITE` on the devfs vnode. `devfs_write_f` in `/usr/src/sys/fs/devfs/devfs_vnops.c` acquires the thread-count reference on the cdev, composes the `ioflag` from `fp->f_flag`, and calls `cdevsw->d_write` with the uio describing the caller's buffer.

Your `d_write` handler runs. It retrieves per-open state via `devfs_get_cdevpriv(9)`, checks liveness, takes whatever lock the driver needs around the buffer, clamps the transfer length to whatever space is available, and calls `uiomove(9)` to copy bytes from user space into the kernel buffer. On success, the handler updates its bookkeeping and returns zero. `devfs_write_f` releases the thread-count reference. `vn_write` unwinds through `sys_write`, which computes the byte count from the decrease in `uio_resid` and returns it. The user sees the return value of `write(2)`.

Three things differ from the read chain in substantive ways.

**First, the kernel runs `copyin` inside `uiomove` instead of `copyout`.** Same mechanism, opposite direction. The fault handling is identical: a bad user pointer returns `EFAULT`, a short copy leaves `uio_resid` consistent with whatever did transfer, and the handler just propagates the error code.

**Second, `ioflag` carries `IO_NDELAY` in the same way, but the driver's interpretation is different.** On a read, non-blocking means "return `EAGAIN` if there is no data". On a write, non-blocking means "return `EAGAIN` if there is no space". Symmetric conditions, symmetric errno values.

**Third, the `atime` / `mtime` updates are direction-specific.** `devfs_read_f` stamps `si_atime` if bytes moved; `devfs_write_f` stamps `si_mtime` (and `si_ctime` in some paths) if bytes moved. These are what `stat(2)` on the node reports, and why `ls -lu /dev/myfirst/0` shows different timestamps for reads versus writes. Your driver does not manage these fields; devfs does.

Once you recognise the read and write traces as mirror images, you have internalised most of the character-device dispatch path. Every chapter from here on will add hooks (a `d_poll`, a `d_kqfilter`, a `d_ioctl`, an `mmap` path) that sit on the same chain at slightly different slots. The chain itself stays constant.



## Practical Workflow: Testing Your Driver From the Shell

The base-system tools are your first and best test harness. This section is a short field guide to using them well on a driver you are developing. None of the commands below are new to you, but using them for driver work has a rhythm worth learning explicitly.

### `cat(1)`: the first check

`cat` reads from its arguments and writes to standard output. For a driver that serves a static message or a drained buffer, `cat` is the fastest way to see what the read path produces:

```sh
% cat /dev/myfirst/0
```

If the output is what you expect, the read path is alive. If it is empty, either your driver has nothing to deliver (check `sysctl dev.myfirst.0.stats.bufused`) or your handler is returning EOF on the first call. If the output is garbled, either your buffer is uninitialised or you are handing out bytes past `bufused`.

`cat` opens its argument once and reads from it until EOF. Every `read(2)` is a separate call into your `d_read`. Use `truss(1)` to see how many calls `cat` makes:

```sh
% truss cat /dev/myfirst/0 2>&1 | grep read
```

The output shows each `read(2)` with its arguments and return value. If you expected one read and see three, that tells you about your buffer sizing; if you expected three reads and see one, your handler delivered all the data in a single call.

### `echo(1)` and `printf(1)`: simple writes

`echo` is the quickest way to get a known string into your driver's write path:

```sh
% echo "hello" | sudo tee /dev/myfirst/0 > /dev/null
```

Two things to notice. First, `echo` appends a newline by default; the string you sent is six bytes, not five. Use `echo -n` to suppress the newline when that matters. Second, the `tee` invocation is there to solve a permission problem: shell redirection (`>`) runs with the user's privileges, so a `sudo echo > /dev/myfirst/0` fails to open the node. Piping through `tee`, which runs under `sudo`, sidesteps that.

`printf` gives you more control:

```sh
% printf 'abc' | sudo tee /dev/myfirst/0 > /dev/null
```

Three bytes, no newline. Use `printf '\x41\x42\x43'` for binary patterns.

### `dd(1)`: the precision tool

For any test that needs a specific byte count or a specific block size, `dd` is the right tool. `dd` is also one of the only base-system tools that reports short reads and short writes in its summary, which makes it uniquely useful for testing driver behaviour:

```sh
% sudo dd if=/dev/urandom of=/dev/myfirst/0 bs=128 count=4
4+0 records in
4+0 records out
512 bytes transferred in 0.001234 secs (415000 bytes/sec)
```

The `X+Y records in` / `X+Y records out` counters have a precise meaning: `X` is the number of full-block transfers, `Y` is the number of short transfers. A line reading `0+4 records out` means every block was accepted only partially. That is a driver telling you something.

`dd` also lets you read with a known block size:

```sh
% sudo dd if=/dev/myfirst/0 of=/tmp/dump bs=64 count=1
```

This issues exactly one `read(2)` for 64 bytes. Your handler sees `uio_resid = 64`; you respond with whatever you have; the result is what `dd` writes to `/tmp/dump`.

The `iflag=fullblock` flag tells `dd` to loop on short reads until it has filled the requested block. Useful when you want to soak all of the driver's output without losing bytes to the short-read default.

### `od(1)` and `hexdump(1)`: byte-level inspection

For driver testing, `od` and `hexdump` let you see the exact bytes your driver emitted:

```sh
% sudo dd if=/dev/myfirst/0 bs=32 count=1 | od -An -tx1z
  68 65 6c 6c 6f 0a                                 >hello.<
```

The `-An` flag suppresses address printing. `-tx1z` shows bytes in hex and ASCII. If the expected output is text, you see it on the right; if it is binary, you see the hex on the left.

These tools become essential when a read produces unexpected bytes. "It looks weird" and "I can see every byte in hex" are very different debugging states.

### `sysctl(8)` and `dmesg(8)`: the kernel's voice

Your driver publishes counters through `sysctl` and lifecycle events through `dmesg`. Both are worth checking during every test:

```sh
% sysctl dev.myfirst.0
% dmesg | tail -20
```

The sysctl output is your view into the driver's state right now. `dmesg` is your view into the driver's history since boot (or since the ring buffer wrapped).

A useful habit: after every test, run both. If the numbers do not match your expectation, you have narrowed down the bug quickly.

### `fstat(1)`: who has the descriptor open?

When your driver refuses to unload ("module busy"), the question is "who has `/dev/myfirst/0` open right now?". `fstat(1)` answers it:

```sh
% fstat -p $(pgrep cat) /dev/myfirst/0
USER     CMD          PID   FD MOUNT      INUM MODE         SZ|DV R/W NAME
ebrandi  cat          1234    3 /dev         0 crw-rw----  myfirst/0  r /dev/myfirst/0
```

Alternatively, `fuser(8)`:

```sh
% sudo fuser /dev/myfirst/0
/dev/myfirst/0:         1234
```

Either tool names the processes holding the descriptor. Kill the culprit (carefully; do not kill anything you did not start) and the module will unload.

### `truss(1)` and `ktrace(1)`: watching the syscalls

For a user program whose interaction with your driver you want to inspect, `truss` shows every syscall and its return value:

```sh
% truss ./rw_myfirst
open("/dev/myfirst/0",O_WRONLY,0666)             = 3 (0x3)
write(3,"round-trip test payload\n",24)          = 24 (0x18)
close(3)                                         = 0 (0x0)
...
```

`ktrace` records to a file that `kdump` prints later; it is the right tool when you want to capture a trace of a long-running program.

These two tools are not driver-specific, but they are how you confirm from the outside that your driver is producing the results a user program will see.

### A Suggested Test Rhythm

For each stage of the chapter, try this loop:

1. Build and load.
2. `cat` to produce initial output, confirm by eye.
3. `sysctl dev.myfirst.0` to see counters match.
4. `dmesg | tail` to see lifecycle events.
5. Write something with `echo` or `dd`.
6. Read it back.
7. Repeat with a larger size, a boundary size, and a pathological size.
8. Unload.

After a couple of iterations this becomes automatic and fast. It is the kind of rhythm that turns driver development from a slog into a routine.

### A Concrete `truss` Walkthrough

Running a userland program under `truss(1)` is one of the fastest ways to see exactly what syscalls it makes to your driver and what return values the kernel produces. Here is a typical session with the Stage 3 driver loaded and empty:

```sh
% truss ./rw_myfirst rt 2>&1
open("/dev/myfirst/0",O_WRONLY,00)               = 3 (0x3)
write(3,"round-trip test payload, 24b\n",29)     = 29 (0x1d)
close(3)                                         = 0 (0x0)
open("/dev/myfirst/0",O_RDONLY,00)               = 3 (0x3)
read(3,"round-trip test payload, 24b\n",255) = 29 (0x1d)
close(3)                                         = 0 (0x0)
exit(0x0)
```

A few things are worth pausing on. Each line shows a single syscall, its arguments, and its return value in both decimal and hex. The `write` call received 29 bytes and the driver accepted all 29 (the return value matches the request length). The `read` call received a buffer of 255 bytes of room and the driver produced 29 bytes of content; a short read, which the user program explicitly accepts. Both `open` calls returned 3, because file descriptors 0, 1, and 2 are standard streams and the first free descriptor is 3.

If you force a short write by limiting the driver, `truss` will show it plainly:

```sh
% truss ./write_big 2>&1 | head
open("/dev/myfirst/0",O_WRONLY,00)               = 3 (0x3)
write(3,"<8192 bytes of data>",8192)             = 4096 (0x1000)
write(3,"<4096 bytes of data>",4096)             ERR#28 'No space left on device'
close(3)                                         = 0 (0x0)
```

The first write requested 8192 bytes and was accepted for 4096. The second write had nothing to say because the buffer is full; the driver returned `ENOSPC`, which `truss` rendered as `ERR#28 'No space left on device'`. This is the view from the user side; your driver side was returning zero (with `uio_resid` decremented to 4096) for the first call and `ENOSPC` for the second. Comparing what `truss` sees against what your `device_printf` says is an excellent way to catch mismatches between the driver's intent and the kernel's reporting.

`truss -f` follows forks, which is useful when your test harness spawns worker processes. `truss -d` prefixes each line with a relative timestamp; useful for reasoning about latency between calls. Both flags are small investments; the rewards add up quickly when you start running multi-process stress tests.

### A Quick Note on `ktrace`

`ktrace(1)` is `truss`'s bigger sibling. It records a binary trace to a file (`ktrace.out` by default) which you then format with `kdump(1)`. It is the right tool when:

- The test run is long and you do not want to watch output live.
- You want to capture detail that is too fine-grained for `truss` (syscall timing, signal delivery, namei lookups).
- You want to replay a trace later, perhaps on a different machine.

A typical session:

```sh
% sudo ktrace -i ./stress_rw -s 5
% sudo kdump | head -40
  2345 stress_rw CALL  open(0x800123456,0x1<O_WRONLY>)
  2345 stress_rw NAMI  "/dev/myfirst/0"
  2345 stress_rw RET   open 3
  2345 stress_rw CALL  write(0x3,0x800123500,0x40)
  2345 stress_rw RET   write 64
  2345 stress_rw CALL  write(0x3,0x800123500,0x40)
  2345 stress_rw RET   write 64
...
```

For Chapter 9 the difference between `truss` and `ktrace` is small. Use `truss` as the default; reach for `ktrace` when you need more detail or a recorded trace.

### Watching Kernel Memory With `vmstat -m`

Your driver allocates kernel memory through `malloc(9)` with the `M_DEVBUF` type. FreeBSD's `vmstat -m` reveals how many allocations are active in each type bucket. Run it while your driver is loaded and idle, then again while it has a buffer allocated, and the increase will be visible in the `devbuf` row:

```sh
% vmstat -m | head -1
         Type InUse MemUse HighUse Requests  Size(s)
% vmstat -m | grep devbuf
       devbuf   415   4120K       -    39852  16,32,64,128,256,512,1024,2048,...
```

The `InUse` column is the current count of live allocations of this type. `MemUse` is the total size currently in use. `HighUse` is the all-time high-water mark since boot. `Requests` is the lifetime count of `malloc` calls that selected this type.

Load the Stage 2 driver. `InUse` goes up by one (the 4096-byte buffer), `MemUse` goes up by approximately 4 KiB, and `Requests` increments. Unload. `InUse` goes down by one; `MemUse` goes down by the 4 KiB. If it does not, you have a memory leak, and `vmstat -m` just told you so.

This is the second observability channel worth adding to your test rhythm. `sysctl` shows driver-owned counters. `dmesg` shows driver-owned log lines. `vmstat -m` shows kernel-owned allocation counts, and it catches a class of bug (forgot to free) that the first two cannot see.

For a driver that declares its own malloc type via `MALLOC_DEFINE(M_MYFIRST, "myfirst", ...)`, `vmstat -m | grep myfirst` is even better: it isolates your driver's allocations from the generic `devbuf` pool. `myfirst` stays with `M_DEVBUF` throughout this chapter for simplicity, but upgrading to a dedicated type is a small change you may want to make before shipping a driver outside the book's lab environment.



## Observability: Making Your Driver Legible

A driver that does the right thing is worth more if you can confirm, from outside the kernel, that it is doing the right thing. This section is a short meditation on the observability choices this chapter has been making, and why.

### Three Surfaces: sysctl, dmesg, userland

Your driver presents three surfaces to the operator:

- **sysctl** for live counters: point-in-time values the operator can poll.
- **dmesg (device_printf)** for lifecycle events: open, close, errors, transitions.
- **/dev** nodes for the data path: the actual bytes.

Each has a distinct role. sysctl tells the operator *what is true right now*. dmesg tells the operator *what changed recently*. `/dev` is the thing the operator is actually using.

A well-observed driver uses all three, deliberately. A minimally-observed driver uses only the third, and debugging it requires either a debugger or a lot of guessing.

### Sysctl: Counters vs State

`myfirst` exposes counters through the sysctl tree under `dev.myfirst.0.stats`:

- `attach_ticks`: a point-in-time value (when the driver attached).
- `open_count`: a monotonically-increasing counter (lifetime opens).
- `active_fhs`: a live count (current descriptors).
- `bytes_read`, `bytes_written`: monotonically-increasing counters.
- `bufused`: a live value (current buffer occupancy).

Monotonically-increasing counters are easier to reason about than live values, because their rate of change is informative even when the absolute value is not. An operator who sees `bytes_read` increasing at 1 MB/s has learned something even if 1 MB/s is meaningless out of context.

Live values are essential when the state matters for decisions (`active_fhs > 0` means unload will fail). Choose monotonically-increasing counters first, live values when you need them.

### dmesg: Events Worth Seeing

`device_printf(9)` writes to the kernel message buffer, which `dmesg` shows. Every line is worth seeing exactly once: use dmesg for events, not for continuous status.

The events `myfirst` logs:

- Attach (once per instance).
- Open (once per open).
- Destructor (once per descriptor close).
- Detach (once per instance).

That is four lines per instance per load/unload cycle, plus two lines per open/close pair. Comfortable.

What we do not log:

- Every `read` or `write` call. That would flood dmesg in any real workload.
- Every sysctl read. Those are passive.
- Every successful transfer. The sysctl counters carry that information, and they carry it more compactly.

If a driver needs to log something that happens many times a second, the usual answer is to guard the logging with `if (bootverbose)`, so it is silent on production systems but available to developers who boot with `boot -v`. For `myfirst` we do not need even that.

### The Trap of Over-Logging

A driver that logs every operation is a driver that hides its important events in a sea of noise. If your dmesg shows ten thousand lines of `read returned 0 bytes`, the line that says `buffer full, returning ENOSPC` is invisible.

Keep logs sparse. Log transitions, not states. Log once per instance, not once per call. When in doubt, silence.

### Counters You Will Add Later

Chapters 10 and beyond will extend the counter tree with:

- `reads_blocked`, `writes_blocked`: count of calls that had to sleep (Chapter 10).
- `poll_waiters`: count of active `poll(2)` subscribers (Chapter 10).
- `drain_waits`, `overrun_events`: ring-buffer diagnostics (Chapter 10).

Each one is one more thing an operator can look at to understand what the driver is doing. The pattern is the same: expose the counters, keep the mechanism silent, let the operator decide when to inspect.

### What Your Driver Looks Like Under Light Load

A concrete example is more useful than abstract advice. Load Stage 3, run the companion `stress_rw` program for a few seconds with `sysctl dev.myfirst.0.stats` watching from another terminal, and you see something like this:

**Before `stress_rw` starts:**

```text
dev.myfirst.0.stats.attach_ticks: 12345678
dev.myfirst.0.stats.open_count: 0
dev.myfirst.0.stats.active_fhs: 0
dev.myfirst.0.stats.bytes_read: 0
dev.myfirst.0.stats.bytes_written: 0
dev.myfirst.0.stats.bufused: 0
```

Zero activity, one attach, buffer empty.

**During `stress_rw`, with `watch -n 0.5 sysctl dev.myfirst.0.stats`:**

```text
dev.myfirst.0.stats.attach_ticks: 12345678
dev.myfirst.0.stats.open_count: 2
dev.myfirst.0.stats.active_fhs: 2
dev.myfirst.0.stats.bytes_read: 1358976
dev.myfirst.0.stats.bytes_written: 1359040
dev.myfirst.0.stats.bufused: 64
```

Two active descriptors (writer + reader), counters climbing, buffer holding 64 bytes of in-flight data. `bytes_written` is slightly ahead of `bytes_read`, which is exactly what you would expect: the writer produced a chunk the reader has not quite consumed yet. The difference equals `bufused`.

**After `stress_rw` exits:**

```text
dev.myfirst.0.stats.attach_ticks: 12345678
dev.myfirst.0.stats.open_count: 2
dev.myfirst.0.stats.active_fhs: 0
dev.myfirst.0.stats.bytes_read: 4800000
dev.myfirst.0.stats.bytes_written: 4800000
dev.myfirst.0.stats.bufused: 0
```

Both descriptors closed. Lifetime opens is 2 (cumulative). Active is 0. `bytes_read` equals `bytes_written`; the reader caught up fully. Buffer is empty.

Three signatures to notice. First, `active_fhs` always tracks live descriptors; it is a live value, not a cumulative counter. Second, `bytes_read == bytes_written` at steady state when the reader is keeping up, plus whatever is sitting in `bufused`. Third, the `open_count` is a lifetime value that never decreases; a quick way to spot churn is to watch it grow while `active_fhs` stays stable.

A driver that behaves predictably under load is a driver you can operate with confidence. Once the counters line up the way this paragraph describes, you have your first real driver, not a toy.



## Signed, Unsigned, and the Perils of Off-by-One

A short section on a class of bug that has caused more kernel panics than almost any other. It shows up especially often in I/O handlers.

### `ssize_t` vs `size_t`

Two types dominate I/O code:

- `size_t`: unsigned, used for sizes and counts. `sizeof(x)` returns `size_t`. `malloc(9)` takes `size_t`. `memcpy` takes `size_t`.
- `ssize_t`: signed, used when a value could be negative (usually -1 for error). `read(2)` and `write(2)` return `ssize_t`. `uio_resid` is `ssize_t`.

The two types have the same width on every platform FreeBSD supports, but they do not silently convert between each other without warnings, and they behave very differently when arithmetic underflows.

A subtraction of `size_t` values that would produce a negative result instead wraps around to a huge positive value, because `size_t` is unsigned. For example:

```c
size_t avail = sc->buflen - sc->bufused;
```

If `sc->bufused` is larger than `sc->buflen`, `avail` is an enormous number, and the next `uiomove` attempts a transfer that blows past the end of the buffer.

The defence is the invariant. In every buffer-management section of the chapter, we maintain `sc->bufhead + sc->bufused <= sc->buflen`. As long as that invariant holds, `sc->buflen - (sc->bufhead + sc->bufused)` cannot underflow.

The risk is in code paths that violate the invariant accidentally. A double-free that restores an already-consumed value; a write that updates `bufused` twice; a race between writers. Those are the bugs to hunt for when `avail` ever looks wrong.

### `uio_resid` Can Be Compared Against Unsigned

`uio_resid` is `ssize_t`. Your buffer sizes are `size_t`. Code like this:

```c
if (uio->uio_resid > sc->buflen) ...
```

Will be compiled with a signed-vs-unsigned comparison. Modern compilers warn about this; the warning should be taken seriously.

The safer pattern is to cast explicitly:

```c
if ((size_t)uio->uio_resid > sc->buflen) ...
```

Or to use `MIN`, which we have been using:

```c
towrite = MIN((size_t)uio->uio_resid, avail);
```

The cast is defensible because `uio_resid` is documented to be non-negative in valid uios (and `uiomove` `KASSERT`s on it). The cast makes the compiler happy and makes the intent explicit.

### Off-by-One in Counters

A counter updated on the wrong side of an error check is a classic bug:

```c
sc->bytes_read += towrite;          /* BAD: happens even on error */
error = uiomove(sc->buf, towrite, uio);
```

The correct shape is to increment after success:

```c
error = uiomove(sc->buf, towrite, uio);
if (error == 0)
        sc->bytes_read += towrite;
```

This is why we have `if (error == 0)` guarding every counter update in the chapter. The cost is one line of code. The benefit is that your counters match reality.

### The `uio_offset - before` Idiom

When you want to know "how many bytes did `uiomove` actually move?", the cleanest way is to compare `uio_offset` before and after:

```c
off_t before = uio->uio_offset;
error = uiomove_frombuf(sc->buf, sc->buflen, uio);
size_t moved = uio->uio_offset - before;
```

This works for both full and short transfers. `moved` is the actual byte count, regardless of what the caller asked for or how much was available.

The idiom is free at runtime (two subtractions) and unambiguous in code. Use it when your driver wants to count bytes; the alternative, inferring the count from `uio_resid`, requires knowing the original request size, which is more bookkeeping.



## Additional Troubleshooting: The Edge Cases

Expanding on the earlier troubleshooting section, here are a few more scenarios you are likely to hit the first time you write a real driver.

### "The second read on the same descriptor returns zero"

Expected for a static-message driver (Stage 1): once `uio_offset` reaches the end of the message, `uiomove_frombuf` returns zero.

Unexpected for a FIFO driver (Stage 3): the first read drained the buffer, and no writer has refilled it. The caller should not be issuing a second read back-to-back without a write happening in between.

To distinguish the two cases, check `sysctl dev.myfirst.0.stats.bufused`. If it is zero, the buffer is empty. If it is non-zero and you still see zero bytes, you have a bug.

### "The driver returns zero bytes immediately when the buffer has data"

The read handler is taking the wrong branch. Common causes:

- A `bufused == 0` check placed in the wrong spot. If the check runs before the per-open state retrieval, it might short-circuit the read before the real work.
- An accidental `return 0;` earlier in the handler (for example, in a debug branch left from a previous experiment).
- A missing `mtx_unlock` on an error path, making every subsequent call block on the mutex forever. Symptom: the second call hangs, not a zero-byte return; but it is worth checking.

### "My `uiomove_frombuf` always returns zero regardless of the buffer"

Two common causes:

- The `buflen` argument is zero. `uiomove_frombuf` returns zero immediately if `buflen <= 0`.
- `uio_offset` is already at or past `buflen`. `uiomove_frombuf` returns zero to signal EOF in that case.

Add a `device_printf` logging the arguments at entry to confirm which case you are in.

### "The buffer overflows into adjacent memory"

Your arithmetic is wrong. Somewhere you are calling `uiomove(sc->buf + X, N, uio)` where `X + N > sc->buflen`. The write proceeds silently and corrupts kernel memory.

Your kernel will usually panic shortly thereafter, possibly in a completely unrelated subsystem. The panic message will not mention your driver; it will mention whichever heap neighbour got clobbered.

If you suspect this, rebuild with `INVARIANTS` and `WITNESS` (and on many targets, KASAN on amd64). These kernel features catch buffer overruns much earlier than the default kernel does.

### "A process reading from the device hangs forever"

Since Chapter 9 does not implement blocking I/O, this should not happen with `myfirst` Stage 3. If it does, the most likely cause is the process holding a file descriptor while you tried to unload the driver; `destroy_dev(9)` is waiting for `si_threadcount` to reach zero, and the process is sitting inside your handler for some reason.

To diagnose: `ps auxH | grep <your-test>`; `gdb -p <pid>` and `bt`. The stack should reveal where the thread is parked.

If your Stage 3 handler accidentally sleeps (for instance, because you added a `tsleep` while experimenting with Chapter 10 material early), the fix is to remove the sleep. Chapter 9's driver does not block.

### "`kldunload` says `kldunload: can't unload file: Device busy`"

Classic symptom of a descriptor still open. Use `fuser /dev/myfirst/0` to find the offending process, close the descriptor or kill the process, and retry.

### "I modified the driver and `make` compiles but `kldload` fails with version mismatch"

Your build environment does not match your running kernel. Check:

```sh
% freebsd-version -k
14.3-RELEASE
% ls /usr/obj/usr/src/amd64.amd64/sys/GENERIC
```

If `/usr/src` is for a different release, your headers produce a module that the kernel refuses. Rebuild against the matching sources. In a lab VM this usually means syncing `/usr/src` with the running release via `fetch` or `freebsd-update src-install`.

### "I see every byte written through the device printed twice in dmesg"

You have a `device_printf` inside the hot path that prints every transfer. Remove it or guard it with `if (bootverbose)`.

A milder version of the same bug: a single-line log that prints the length of every transfer. For small test workloads that looks fine; for a real user workload it will bury dmesg and cause timestamp compression in the kernel buffer.

### "My `d_read` is called but my `d_write` is not"

Either the user program never calls `write(2)` on the device, or it calls `write(2)` with the descriptor not opened for writing (`O_RDONLY`). Check both.

Also: confirm that `cdevsw.d_write` is assigned to `myfirst_write`. A copy-paste bug that assigns it to `myfirst_read` results in both directions hitting the read handler, with predictably confusing results.



## Design Notes: Why Each Stage Stops Where It Does

A short meta-section on why Chapter 9's three stages have the boundaries they have. This is the kind of chapter-design reasoning that is worth making explicit, because it is the reasoning you will apply when you design your own drivers.

### Why Stage 1 Exists

Stage 1 is the smallest possible `d_read` that is not `/dev/null`. It introduces:

- The `uiomove_frombuf(9)` helper, the easiest way to get a fixed buffer out to user space.
- Per-descriptor offset handling.
- The pattern of using `uio_offset` as the state carrier.

Stage 1 does not do anything with writes; the Chapter 8 stub is fine.

Without Stage 1, the jump from stubs to a buffered read/write driver is too large. Stage 1 lets you confirm, with minimal code, that the read handler is wired up correctly. Everything else builds on that confirmation.

### Why Stage 2 Exists

Stage 2 introduces:

- A dynamically-allocated kernel buffer.
- A write path that accepts user data.
- A read path that honours the caller's offset across the accumulating buffer.
- The first realistic use of the softc mutex in an I/O handler.

Stage 2 deliberately does not drain reads. The buffer grows until full; subsequent writes return `ENOSPC`. This lets two concurrent readers confirm that they each have their own `uio_offset`, which is the property Stage 1 couldn't demonstrate (because Stage 1 had nothing to write).

### Why Stage 3 Exists

Stage 3 introduces:

- Reads that drain the buffer.
- The coordination between a head pointer and a used count.
- The FIFO semantics that most real drivers approximate.

Stage 3 does not wrap around. The head and used pointers walk forward through the buffer and the buffer collapses to the beginning when empty. A proper ring buffer (with head and tail wrapping around a fixed-size array) belongs in Chapter 10 because it pairs naturally with blocking reads and `poll(2)`: a ring makes steady-state operation efficient, and efficient steady-state operation is exactly what a blocking reader needs.

### Why No Ring Buffer Here

A ring buffer is five to fifteen lines of additional bookkeeping beyond what Stage 3 does. Adding it now would not be a large amount of code. The reason it is deferred is pedagogical: the two concepts ("I/O path semantics" and "ring buffer mechanics") are independently confusing to a beginner, and splitting them into two chapters lets each chapter address one pile of confusion at a time.

By the time Chapter 10 introduces the ring, the reader is fluent in the I/O path. The new material is only the ring bookkeeping.

### Why No Blocking

Blocking is useful, but it introduces `msleep(9)`, condition variables, the `d_purge` teardown hook, and a thicket of correctness issues around what to wake and when. Each of those is a substantial topic. Mixing them into Chapter 9 would double its length and halve its clarity.

Chapter 10's first section is "when your driver has to wait". It is a natural follow-on.

### What the Stages Are **Not** Trying to Be

The stages are not a simulation of a hardware driver. They do not mimic DMA. They do not simulate interrupts. They do not pretend to be anything other than what they are: in-memory drivers that exercise the UNIX I/O path.

This matters because later in the book, when we write actual hardware drivers, the I/O path will look identical. The hardware specifics (where the bytes come from, where the bytes go to) will change, but the handler shape, the uiomove usage, the errno conventions, the counter patterns, all of these will be recognisable from Chapter 9.

A driver that moves bytes correctly across the user/kernel trust boundary is 80% of any real driver. Chapter 9 teaches that 80%.



## Hands-On Labs

The labs below track the three stages above. Each lab is a checkpoint that proves your driver is doing the thing the text just described. Read the lab fully before starting, and do them in order.

### Lab 9.1: Build and Load Stage 1

**Goal:** Build the Stage 1 driver, load it, read the static message, and confirm per-descriptor offset handling.

**Steps:**

1. Start from the companion tree: `cp -r examples/part-02/ch09-reading-and-writing/stage1-static-message ~/drivers/ch09-stage1`. Alternatively, modify your Chapter 8 stage 2 driver according to the Stage 1 walkthrough above.
2. Change into the directory and build:
   ```sh
   % cd ~/drivers/ch09-stage1
   % make
   ```
3. Load the module:
   ```sh
   % sudo kldload ./myfirst.ko
   ```
4. Confirm the device is present:
   ```sh
   % ls -l /dev/myfirst/0
   crw-rw----  1 root  operator ... /dev/myfirst/0
   ```
5. Read the message:
   ```sh
   % cat /dev/myfirst/0
   Hello from myfirst.
   This is your first real read path.
   Chapter 9, Stage 1.
   ```
6. Build the `rw_myfirst.c` userland tool from the companion tree and run it in "read twice" mode:
   ```sh
   % cc -o rw_myfirst rw_myfirst.c
   % ./rw_myfirst read
   [read 1] 75 bytes:
   Hello from myfirst.
   This is your first real read path.
   Chapter 9, Stage 1.
   [read 2] 0 bytes (EOF)
   ```
7. Confirm the per-descriptor counter:
   ```sh
   % dmesg | tail -5
   ```
   You should see the `open via /dev/myfirst/0 fh=...` and `per-open dtor fh=...` lines from Chapter 8, plus the message body was read.
8. Unload:
   ```sh
   % sudo kldunload myfirst
   ```

**Success criteria:**

- `cat` prints the message.
- The userland tool shows 75 bytes on the first read and 0 bytes on the second.
- `dmesg` shows one open and one destructor per `./rw_myfirst read` invocation.

**Common mistakes:**

- Forgetting the `-1` on `sizeof(myfirst_message) - 1`. The message will include a trailing NUL byte that appears as a stray character in user output.
- Not calling `devfs_get_cdevpriv` before the `sc == NULL` check. The rest of the chapter depends on this order; run it to see why it is the right one.
- Using `(void *)sc->message` instead of `__DECONST(void *, sc->message)`. Both work on most compilers; the `__DECONST` form is the convention and suppresses a warning on some compiler configurations.

### Lab 9.2: Exercise Stage 2 with Writes and Reads

**Goal:** Build Stage 2, push data in from userland, pull it back out, and observe the sysctl counters.

**Steps:**

1. From the companion tree: `cp -r examples/part-02/ch09-reading-and-writing/stage2-readwrite ~/drivers/ch09-stage2`.
2. Build and load:
   ```sh
   % cd ~/drivers/ch09-stage2
   % make
   % sudo kldload ./myfirst.ko
   ```
3. Check the initial state:
   ```sh
   % sysctl dev.myfirst.0.stats
   dev.myfirst.0.stats.attach_ticks: ...
   dev.myfirst.0.stats.open_count: 0
   dev.myfirst.0.stats.active_fhs: 0
   dev.myfirst.0.stats.bytes_read: 0
   dev.myfirst.0.stats.bytes_written: 0
   dev.myfirst.0.stats.bufused: 0
   ```
4. Write a line of text:
   ```sh
   % echo "the quick brown fox" | sudo tee /dev/myfirst/0 > /dev/null
   ```
5. Read it back:
   ```sh
   % cat /dev/myfirst/0
   the quick brown fox
   ```
6. Observe the counters:
   ```sh
   % sysctl dev.myfirst.0.stats.bufused
   dev.myfirst.0.stats.bufused: 20
   % sysctl dev.myfirst.0.stats.bytes_written
   dev.myfirst.0.stats.bytes_written: 20
   % sysctl dev.myfirst.0.stats.bytes_read
   dev.myfirst.0.stats.bytes_read: 20
   ```
7. Trigger `ENOSPC`:
   ```sh
   % dd if=/dev/zero bs=1024 count=8 | sudo tee /dev/myfirst/0 > /dev/null
   ```
   Expect a short-write error. Inspect `sysctl dev.myfirst.0.stats.bufused`; it should be 4096 (the buffer size).
8. Confirm reads still deliver the content:
   ```sh
   % sudo cat /dev/myfirst/0 | od -An -c | head -3
   ```
9. Unload:
   ```sh
   % sudo kldunload myfirst
   ```

**Success criteria:**

- Writes deposit bytes; reads deliver them back.
- `bufused` matches the number of bytes written since the last reset.
- `dd` exhibits a short write when the buffer fills; the driver returns `ENOSPC`.
- `dmesg` shows open and destructor lines for every process that opened the device.

**Common mistakes:**

- Forgetting to free `sc->buf` in `detach`. The driver will unload without complaint, but a subsequent kernel memory leak check (`vmstat -m | grep devbuf`) will show drift.
- Holding the softc mutex while calling `uiomove`, without being sure the mutex is an `MTX_DEF` and not a spin lock. Chapter 7's `mtx_init(..., MTX_DEF)` is the right choice; do not change it.
- Omitting the `sc->bufused = 0` reset in `attach`. `Newbus` initialises softc to zero for you, but making the initialisation explicit is the convention; it also makes a later refactor less error-prone.

### Lab 9.3: Stage 3 FIFO Behaviour

**Goal:** Build Stage 3, exercise FIFO behaviour from two terminals, and confirm that reads drain the buffer.

**Steps:**

1. From the companion tree: `cp -r examples/part-02/ch09-reading-and-writing/stage3-echo ~/drivers/ch09-stage3`.
2. Build and load:
   ```sh
   % cd ~/drivers/ch09-stage3
   % make
   % sudo kldload ./myfirst.ko
   ```
3. In terminal A, write some bytes:
   ```sh
   % echo "message A" | sudo tee /dev/myfirst/0 > /dev/null
   ```
4. In terminal B, read them:
   ```sh
   % cat /dev/myfirst/0
   message A
   ```
5. Read again in terminal B:
   ```sh
   % cat /dev/myfirst/0
   ```
   Expect no output. The buffer is empty.
6. In terminal A, write two lines in rapid succession:
   ```sh
   % echo "first" | sudo tee /dev/myfirst/0 > /dev/null
   % echo "second" | sudo tee /dev/myfirst/0 > /dev/null
   ```
7. In terminal B, read:
   ```sh
   % cat /dev/myfirst/0
   first
   second
   ```
   Expect the two lines concatenated. Both writes appended to the same buffer before either read happened.
8. Inspect the counters:
   ```sh
   % sysctl dev.myfirst.0.stats
   ```
   `bufused` should be back to zero. `bytes_read` and `bytes_written` should match.
9. Unload:
   ```sh
   % sudo kldunload myfirst
   ```

**Success criteria:**

- Writes append to the buffer; reads drain it.
- A read after the buffer is drained returns immediately (EOF-on-empty).
- `bytes_read` always equals `bytes_written` once the reader has caught up.

**Common mistakes:**

- Not resetting `bufhead = 0` when `bufused` reaches zero. The buffer will "drift" toward the end of `sc->buf` and refuse writes long before it is full.
- Forgetting to update `bufhead` as reads drain. The driver will read the same bytes repeatedly.
- Using `uio->uio_offset` as a per-descriptor offset. In a FIFO, offsets are shared; a per-descriptor offset does not make sense and will confuse testers.

### Lab 9.4: Using `dd` to Measure Transfer Behaviour

**Goal:** Use `dd(1)` to generate known-size transfers, read the results back, and check that the counters agree.

`dd` is the tool of choice here because it lets you control the block size, the number of blocks, and the behaviour on short transfers.

1. Reload the Stage 3 driver fresh:
   ```sh
   % sudo kldunload myfirst; sudo kldload ./myfirst.ko
   ```
2. Write 512 bytes in a single block:
   ```sh
   % sudo dd if=/dev/urandom of=/dev/myfirst/0 bs=512 count=1
   1+0 records in
   1+0 records out
   512 bytes transferred
   ```
3. Observe `bufused = 512`:
   ```sh
   % sysctl dev.myfirst.0.stats.bufused
   dev.myfirst.0.stats.bufused: 512
   ```
4. Read them back with matching block size:
   ```sh
   % sudo dd if=/dev/myfirst/0 of=/tmp/out bs=512 count=1
   1+0 records in
   1+0 records out
   512 bytes transferred
   ```
5. Check that the FIFO is now empty:
   ```sh
   % sysctl dev.myfirst.0.stats.bufused
   dev.myfirst.0.stats.bufused: 0
   ```
6. Write 8192 bytes in one big block:
   ```sh
   % sudo dd if=/dev/urandom of=/dev/myfirst/0 bs=8192 count=1
   dd: /dev/myfirst/0: No space left on device
   0+0 records in
   0+0 records out
   0 bytes transferred
   ```
   The driver accepted 4096 bytes (the buffer size) of the 8192 requested and returned a short write for the rest.
7. Alternatively, use `bs=4096` with `count=2`:
   ```sh
   % sudo dd if=/dev/urandom of=/dev/myfirst/0 bs=4096 count=2
   dd: /dev/myfirst/0: No space left on device
   1+0 records in
   0+0 records out
   4096 bytes transferred
   ```
   The first block of 4096 bytes succeeded in full; the second block failed with `ENOSPC`.
8. Drain:
   ```sh
   % sudo dd if=/dev/myfirst/0 of=/tmp/out bs=4096 count=1
   % sudo kldunload myfirst
   ```

**Success criteria:**

- `dd` reports the expected byte counts at each step.
- The driver accepts up to 4096 bytes and refuses the rest with `ENOSPC`.
- `bufused` tracks the buffer state after every operation.

### Lab 9.5: A Small Round-Trip C Program

**Goal:** Write a short userland C program that opens the device, writes known bytes, closes the descriptor, opens it again, reads the bytes back, and verifies they match.

1. Save the following as `rw_myfirst.c` in `~/drivers/ch09-stage3`:

```c
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

static const char payload[] = "round-trip test payload\n";

int
main(void)
{
        int fd;
        ssize_t n;

        fd = open("/dev/myfirst/0", O_WRONLY);
        if (fd < 0) { perror("open W"); return 1; }
        n = write(fd, payload, sizeof(payload) - 1);
        if (n != (ssize_t)(sizeof(payload) - 1)) {
                fprintf(stderr, "short write: %zd\n", n);
                return 2;
        }
        close(fd);

        char buf[128] = {0};
        fd = open("/dev/myfirst/0", O_RDONLY);
        if (fd < 0) { perror("open R"); return 3; }
        n = read(fd, buf, sizeof(buf) - 1);
        if (n < 0) { perror("read"); return 4; }
        close(fd);

        if ((size_t)n != sizeof(payload) - 1 ||
            memcmp(buf, payload, n) != 0) {
                fprintf(stderr, "mismatch: wrote %zu, read %zd\n",
                    sizeof(payload) - 1, n);
                return 5;
        }

        printf("round-trip OK: %zd bytes\n", n);
        return 0;
}
```

2. Build and run:
   ```sh
   % cc -o rw_myfirst rw_myfirst.c
   % sudo ./rw_myfirst
   round-trip OK: 24 bytes
   ```
3. Inspect `dmesg` to see the two opens and two destructors.

**Success criteria:**

- The program prints `round-trip OK: 24 bytes`.
- `dmesg` shows one open/destructor pair for the write and one for the read.

**Common mistakes:**

- Writing fewer bytes than the payload and not checking the return value. `write(2)` can return a short count; your test must handle it.
- Forgetting `O_WRONLY` vs `O_RDONLY`. `open(2)` enforces the mode against the access bits of the node; opening with the wrong mode returns `EACCES` (or similar).
- Assuming `read(2)` returns the requested count. It can return less; again, the caller loops.

### Lab 9.6: Inspecting Binary Round-Trips

**Goal:** Confirm that the driver handles arbitrary binary data, not only text, by pushing random bytes through and checking that the same bytes come back.

1. With Stage 3 loaded and empty, write 256 random bytes:
   ```sh
   % sudo dd if=/dev/urandom of=/tmp/sent bs=256 count=1
   % sudo dd if=/tmp/sent of=/dev/myfirst/0 bs=256 count=1
   ```
2. Read the same number of bytes back:
   ```sh
   % sudo dd if=/dev/myfirst/0 of=/tmp/received bs=256 count=1
   ```
3. Compare:
   ```sh
   % cmp /tmp/sent /tmp/received && echo MATCH
   MATCH
   ```
4. Inspect both files byte-for-byte:
   ```sh
   % od -An -tx1 /tmp/sent | head -2
   % od -An -tx1 /tmp/received | head -2
   ```
5. Try a pathological pattern: all zeros, all `0xff`, then a file full of a single byte. Confirm every pattern round-trips exactly.

**Success criteria:**

- `cmp` reports no differences.
- The driver preserves every bit of the input.
- No byte-ordering, no "helpful" interpretation, no surprise transformations.

This lab is short but important: it verifies that your driver is a transparent byte store, not a text filter that accidentally interprets some bytes specially. If you ever see differences between the sent and received files, you have a bug in the transfer path, probably a length miscount or an off-by-one in the buffer arithmetic.

### Lab 9.7: Observing a Running Driver End-to-End

**Goal:** Combine sysctl, dmesg, truss, and vmstat into a single end-to-end observation of the Stage 3 driver under real load. This lab has no new code; it is the bridge from "I wrote the driver" to "I can see what it is doing".

**Steps:**

1. With Stage 3 loaded fresh, open four terminals. Terminal A will run the driver load / unload cycles. Terminal B will monitor sysctl. Terminal C will tail dmesg. Terminal D will run a user workload.
2. **Terminal A:**
   ```sh
   % sudo kldload ./myfirst.ko
   % vmstat -m | grep devbuf
   ```
   Note the `devbuf` row's `InUse` and `MemUse` values.
3. **Terminal B:**
   ```sh
   % watch -n 1 sysctl dev.myfirst.0.stats
   ```
4. **Terminal C:**
   ```sh
   % sudo dmesg -c > /dev/null
   % sudo dmesg -w
   ```
   The `-c` clears accumulated messages; the `-w` watches for new ones.
5. **Terminal D:**
   ```sh
   % cd examples/part-02/ch09-reading-and-writing/userland
   % make
   % sudo truss ./rw_myfirst rt 2>&1 | tail -10
   ```
6. Check terminal B: you should see `open_count` increment by 2 (one for the write, one for the read), `active_fhs` return to 0, and `bytes_read == bytes_written`.
7. Check terminal C: you should see two open lines and two destructor lines from `device_printf`.
8. In terminal A, run `vmstat -m | grep devbuf` again. `InUse` and `MemUse` should have decreased back to their pre-load values plus whatever the driver itself allocated (typically just the 4 KiB buffer and the softc).
9. **Stress run:** in terminal D,
   ```sh
   % sudo ./stress_rw -s 5
   ```
   Watch terminal B. You should see `bufused` oscillate, counters climb, and `active_fhs` hit 2 while the test runs.
10. When the stress run finishes, in terminal B verify `active_fhs` is 0. In terminal A,
    ```sh
    % sudo kldunload myfirst
    % vmstat -m | grep devbuf
    ```
    `InUse` should have returned to its pre-load baseline. If it has not, your driver leaked an allocation and `vmstat -m` just told you.

**Success criteria:**

- Sysctl counters match the workload you ran.
- Dmesg shows one open/destructor pair per descriptor open/close.
- Truss output matches your mental model of what the program did.
- `vmstat -m | grep devbuf` returns to its baseline after unload.
- No panics, no warnings, no unexplained counter drift.

**Why this lab matters:** this is the first lab that exercises the full observability toolchain at once. In production, the signal that something is wrong almost never comes from a crash; it comes from a counter that has drifted out of bounds, a `dmesg` line nobody expected, or a `vmstat -m` reading that does not match reality. Building the habit of looking at all four surfaces together is what separates "I wrote a driver" from "I am responsible for a driver".



## Challenge Exercises

These challenges stretch the material without introducing topics that belong to later chapters. Each one uses only the primitives we have introduced. Try them before looking at the companion tree; the learning is in the attempt, not the answer.

### Challenge 9.1: Per-Descriptor Read Counters

Extend Stage 2 so that the per-descriptor `reads` counter is exposed via a sysctl. The counter should be available per active descriptor, which means a per-`fh` sysctl rather than a per-softc one.

This challenge is harder than it looks: sysctls are allocated and freed at known points in the softc lifecycle, and the per-descriptor structure lives only as long as its descriptor. A clean solution registers a sysctl node per `fh` in `d_open` and unregisters it in the destructor. Be careful about lifetimes; the sysctl context must be freed before the `fh` memory.

*Hint:* `sysctl_ctx_init` and `sysctl_ctx_free` are per-context. You can give each `fh` its own context, and free it in the destructor.

*Alternative:* keep a linked list of `fh` pointers in the softc (under the mutex) and expose it through a custom sysctl handler that walks the list on demand. This is the pattern `/usr/src/sys/kern/tty_info.c` uses for per-process stats.

### Challenge 9.2: A readv(2)-Aware Test

Write a user program that uses `readv(2)` to read from the driver into three separate buffers of sizes 8, 16, and 32 bytes. Confirm that the driver delivers bytes into all three buffers in sequence.

The kernel and `uiomove(9)` already handle `readv(2)`; the driver does not need changes. The purpose of this challenge is to convince yourself of that fact.

*Hint:* `struct iovec iov[3] = {{buf1, 8}, {buf2, 16}, {buf3, 32}};`, then `readv(fd, iov, 3)`. The return value is the total bytes delivered across all three buffers; the individual `iov_len` values are not modified on the user side.

### Challenge 9.3: Short-Write Demonstration

Modify Stage 2's `myfirst_write` to accept at most 128 bytes per call, regardless of `uio_resid`. A user program that writes 1024 bytes should see a short write of 128 every time.

Then write a short test program that writes 1024 bytes in a single `write(2)` call, observes the short-write return value, and loops until all 1024 bytes have been accepted.

Questions to think through:

- Does `cat` handle short writes correctly? (Yes.)
- Does `echo > /dev/myfirst/0 "..."` handle them correctly? (Usually, via `printf` in the shell, but sometimes not; worth testing.)
- What happens if you remove the short-write behaviour and try to exceed the buffer size? (You get `ENOSPC` after the first 4096-byte write.)

This challenge teaches you to separate "the driver does the right thing" from "user programs assume what drivers do".

### Challenge 9.4: An `ls -l` Sensor

Make the driver's response to a read depend on the `ls -l` output of the device itself. That is: every read produces the current timestamp of the device node.

*Hint:* `sc->cdev->si_ctime` and `sc->cdev->si_mtime` are `struct timespec` fields on the cdev. You can convert them to a string with `printf` formatting, place the string in a kernel buffer, and `uiomove_frombuf(9)` it out.

*Warning:* `si_ctime` / `si_mtime` may be updated by devfs as nodes are touched. Observe what happens when you `touch /dev/myfirst/0` and read again.

### Challenge 9.5: A Reverse-Echo Driver

Modify Stage 3 so that every read returns the bytes in reverse order from how they were written. A write of `"hello"` followed by a read should produce `"olleh"`.

This challenge is entirely about buffer bookkeeping. The `uiomove` calls stay the same; you change the addresses you hand to them.

*Hint:* You can either reverse the buffer on every read (expensive) or store bytes in reverse order on the write side (cheaper). Neither is the "right" answer; each has different correctness and concurrency properties. Pick one and argue for it in a comment.

### Challenge 9.6: Binary Round Trip

Write a user program that writes a `struct timespec` to the driver, then reads one back. Compare the two structures. Are they equal? They should be, because `myfirst` is a transparent byte store.

Extend the program to write two `struct timespec` values, then `lseek(fd, sizeof(struct timespec), SEEK_SET)` and read the second one. What happens? (Clue: the FIFO does not support seeks meaningfully.)

This challenge illustrates the "read and write carry bytes, not types" point from the safe-data-transfer section. The bytes round-trip perfectly; the type information does not.

### Challenge 9.7: A Hex-View Test Harness

Write a short shell script that, given a byte count N, generates N random bytes with `dd if=/dev/urandom bs=$N count=1`, pipes them into your Stage 3 driver, then reads them back with `dd if=/dev/myfirst/0 bs=$N count=1`, and compares the two streams with `cmp`. The script should report success for matching streams and diff-like output for mismatching streams. Run it with N = 1, 2, 4, ..., 4096 to sweep small, boundary, and capacity-filling sizes.

Questions to answer as you run the sweep:

- Does every size round-trip cleanly up to and including 4096?
- At 4097, what does the driver do? Does the test harness report the error meaningfully?
- Is there any size at which `cmp` reports a difference? If so, what was the underlying cause?

This challenge rewards combining the tools in the Practical Workflow section: `dd` for precise transfers, `cmp` for byte-level verification, `sysctl` for counters, and the shell for orchestration. A robust test harness like this is the kind of habit that pays for itself every time you refactor a driver and want to know quickly whether the behaviour is still right.

### Challenge 9.8: Who Has the Descriptor Open?

Write a small C program that opens `/dev/myfirst/0`, blocks on `pause()` (so it holds the descriptor indefinitely), and runs until `SIGTERM`. In a second terminal, run `fstat | grep myfirst` and then `fuser /dev/myfirst/0`. Note the output. Now try to `kldunload myfirst`. What error do you get? Why?

Now kill the holder with `SIGTERM` or plain `kill`. Observe the destructor fire in `dmesg`. Try `kldunload` again. It should succeed.

This challenge is short, but it grounds one of the chapter's subtler invariants: a driver cannot unload while any descriptor is open on one of its cdevs, and FreeBSD gives operators a standard set of tools to find the holder. The next time a real-world `kldunload` fails with `EBUSY`, you will have seen the shape of the problem before.



## Troubleshooting Common Mistakes

Every `d_read` / `d_write` mistake you are likely to make falls into one of a small number of categories. This section is a short field guide.

### "My driver returns zero bytes even though I wrote data"

This is usually one of two bugs.

**Bug 1**: You forgot to update `bufused` (or equivalent) after the successful `uiomove`. The write arrived, the bytes moved, but the driver's state never reflected the arrival. The next read sees `bufused == 0` and reports EOF.

Fix: always update your tracking fields inside `if (error == 0) { ... }` after `uiomove` returns.

**Bug 2**: You reset `bufused` (or `bufhead`) somewhere inappropriate. A common pattern is adding a reset line inside `d_open` or `d_close` "for cleanliness". That wipes out the data the previous caller wrote.

Fix: reset driver-wide state only in `attach` (at load) or `detach` (at unload). Per-descriptor state belongs in `fh`, reset by `malloc(M_ZERO)` and cleaned up by the destructor.

### "My reads return garbage"

The buffer is uninitialised. `malloc(9)` without `M_ZERO` returns a block of memory whose contents are undefined. If your `d_read` reaches past `bufused`, or reads from offsets that have not been written, the bytes you see are leftovers from whatever memory the kernel recycled.

Fix: always pass `M_ZERO` to `malloc` in `attach`. Always clamp reads to the current high-water mark (`bufused`), not to the buffer's total size (`buflen`).

There is a more serious variant of this bug. A driver that returns uninitialised kernel memory to user space has just leaked kernel state into user space. In production that is a security hole. In development it is a bug; in production it is a CVE.

### "The kernel panics with a pagefault on a user address"

You called `memcpy` or `bcopy` directly on a user pointer instead of going through `uiomove` / `copyin` / `copyout`. The access faulted, the kernel had no fault handler installed, and the result was a panic.

Fix: never dereference a user pointer directly. Route through `uiomove(9)` (in handlers) or `copyin(9)` / `copyout(9)` (in other contexts).

### "The driver refuses to unload"

You have at least one file descriptor still open. `detach` returns `EBUSY` when `active_fhs > 0`; the module will not unload until every `fh` has been destroyed.

Fix: close the descriptor in userland. If a background process is holding it, kill the process (after confirming it is yours; do not kill system daemons). `fstat -p <pid>` shows which files a process has open; `fuser /dev/myfirst/0` shows which processes have the node open.

Chapter 10 will introduce `destroy_dev_drain` patterns for drivers that need to coerce a blocked reader to exit. Chapter 9 does not block, so this issue does not arise in normal operation; when it arises, it is because userland is holding the descriptor somewhere unexpected.

### "My write handler returns EFAULT"

Your `uiomove` call hit an invalid user address. The common causes:

- A user program called `write(fd, NULL, n)` or `write(fd, (void*)0xdeadbeef, n)`.
- A user program wrote a pointer it had freed.
- You accidentally passed a kernel pointer as the destination to `uiomove`. This can happen if you build a uio by hand for kernel-space data and then pass it to a handler expecting a user-space uio. The resulting `copyout` sees a "user" address that is actually a kernel address; depending on the architecture, you either get `EFAULT` or a subtle corruption.

Fix: check `uio->uio_segflg`. For user-driven handlers, it should be `UIO_USERSPACE`. If you are passing around a kernel-space uio, make sure `uio_segflg == UIO_SYSSPACE` and that your code paths know the difference.

### "My counters are wrong under concurrent writes"

Two writers raced on `bufused`. Each read the current value, added to it, and wrote back, and the second writer overwrote the first writer's update with a stale value.

Fix: take `sc->mtx` around every read-modify-write of shared state. Part 3 makes this a first-class topic; for Chapter 9, a single mutex around the whole critical section is enough.

### "sysctl counters do not reflect the real state"

Two variants.

**Variant A**: the counter is a `size_t`, but the sysctl macro is `SYSCTL_ADD_U64`. On 32-bit architectures, the macro reads 8 bytes where the field is only 4 bytes wide; half the value is junk.

Fix: match the sysctl macro to the field type. `size_t` pairs with `SYSCTL_ADD_UINT` on 32-bit platforms and `SYSCTL_ADD_U64` on 64-bit platforms. To be portable, use `uint64_t` for counters and cast when updating.

**Variant B**: the counter is never updated because the update is inside the `if (error == 0)` block and `uiomove` returned a non-zero error. That is actually correct behaviour: you should not count bytes you did not move. The symptom only looks like a bug if you are trying to use the counter to debug the error.

Fix: add an `error_count` counter that ticks on every non-zero return, independently of `bytes_read` and `bytes_written`. Useful for debugging.

### "The first read after a fresh load returns zero bytes"

Usually intentional. In Stage 3, an empty buffer returns zero bytes. If you expected the static message from Stage 1, check that you are running the Stage 1 driver, not a later one.

If it is unintentional, double-check that `attach` is setting `sc->buf`, `sc->buflen`, and `sc->message_len` as expected. A common bug is copy-pasting the attach code from Stage 1 into Stage 2 and leaving the `sc->message = ...` assignment in place, which then takes precedence over the `malloc` line.

### "The build fails with unknown reference to uiomove_frombuf"

You forgot to include `<sys/uio.h>`. Add it to the top of `myfirst.c`.

### "My handler is called twice for one read(2)"

It almost certainly is not. What is more likely: your handler is being called once with `uio_iovcnt > 1` (a `readv(2)` call), and inside `uiomove` each iovec entry is being drained in turn. The internal loop in `uiomove` may make multiple `copyout` calls in what is a single invocation of your handler.

Verify by adding a `device_printf` at entry and exit of your `d_read`. You should see one entry and one exit per user-space `read(2)` call, regardless of iovec count.



## Contrast Patterns: Correct vs. Buggy Handlers

The troubleshooting guide above is reactive: it helps when something has already gone wrong. This section is the prescriptive companion. Each entry shows a plausible but wrong way to write part of a handler, pairs it with the correct rewrite, and explains the distinction. Studying the contrasts in advance is the fastest way to avoid the bugs in the first place.

Read each pair carefully. The correct version is the pattern you should reach for; the buggy version is the shape your own hands may produce when you are moving fast. Recognising the mistake in the wild, months from now, is worth the five minutes it takes to internalise the difference today.

### Contrast 1: Returning a Byte Count

**Buggy:**

```c
static int
myfirst_read(struct cdev *dev, struct uio *uio, int ioflag)
{
        /* ... */
        error = uiomove_frombuf(sc->message, sc->message_len, uio);
        if (error)
                return (error);
        return (sc->message_len); /* BAD: returning a count */
}
```

**Correct:**

```c
static int
myfirst_read(struct cdev *dev, struct uio *uio, int ioflag)
{
        /* ... */
        return (uiomove_frombuf(sc->message, sc->message_len, uio));
}
```

**Why it matters.** The handler's return value is an errno, not a count. The kernel computes the byte count from the change in `uio->uio_resid` and reports it to user space. A non-zero positive return is interpreted as an errno; if you returned `sc->message_len`, the caller would receive a very strange `errno` value. For example, returning `75` would manifest as `errno = 75`, which on FreeBSD happens to be `EPROGMISMATCH`. The bug is both wrong and deeply confusing to anybody looking at it from the user side.

The rule is simple and absolute: handlers return errno values, never counts. If you want to know the byte count, compute it from the uio.

### Contrast 2: Handling a Zero-Length Request

**Buggy:**

```c
static int
myfirst_read(struct cdev *dev, struct uio *uio, int ioflag)
{
        if (uio->uio_resid == 0)
                return (EINVAL); /* BAD: zero-length is legal */
        /* ... */
}
```

**Correct:**

```c
static int
myfirst_read(struct cdev *dev, struct uio *uio, int ioflag)
{
        /* No special case. uiomove handles zero-resid cleanly. */
        return (uiomove_frombuf(sc->message, sc->message_len, uio));
}
```

**Why it matters.** A `read(fd, buf, 0)` call is legal UNIX. A driver that rejects it with `EINVAL` breaks programs that use zero-byte reads to check descriptor state. `uiomove` returns zero immediately if the uio has nothing to move; your handler does not need to special-case it. Special-casing it wrong is worse than not special-casing it at all.

### Contrast 3: Buffer Capacity Arithmetic

**Buggy:**

```c
mtx_lock(&sc->mtx);
avail = sc->buflen - sc->bufused;
towrite = uio->uio_resid;            /* BAD: no clamp */
error = uiomove(sc->buf + sc->bufused, towrite, uio);
if (error == 0)
        sc->bufused += towrite;
mtx_unlock(&sc->mtx);
return (error);
```

**Correct:**

```c
mtx_lock(&sc->mtx);
avail = sc->buflen - sc->bufused;
if (avail == 0) {
        mtx_unlock(&sc->mtx);
        return (ENOSPC);
}
towrite = MIN((size_t)uio->uio_resid, avail);
error = uiomove(sc->buf + sc->bufused, towrite, uio);
if (error == 0)
        sc->bufused += towrite;
mtx_unlock(&sc->mtx);
return (error);
```

**Why it matters.** The buggy version hands `uiomove` a length of `uio_resid`, which may exceed the buffer's remaining capacity. `uiomove` will not move more than `uio_resid` bytes, but the *destination* is `sc->buf + sc->bufused`, and the math does not know about `sc->buflen`. If the user writes 8 KiB into a 4 KiB buffer with `bufused = 0`, the handler will write 4 KiB past the end of `sc->buf`. That is a classic kernel heap overflow: the crash will not be immediate, will not implicate your driver, and may reveal itself as a panic inside a completely unrelated subsystem half a second later.

The correct version clamps the transfer to `avail`, guaranteeing that the pointer arithmetic stays inside the buffer. The clamp is one `MIN` call, and it is not optional.

### Contrast 4: Holding a Spin Lock Across `uiomove`

**Buggy:**

```c
mtx_lock_spin(&sc->spin);            /* BAD: spin lock, not a regular mutex */
error = uiomove(sc->buf + off, n, uio);
mtx_unlock_spin(&sc->spin);
return (error);
```

**Correct:**

```c
mtx_lock(&sc->mtx);                  /* MTX_DEF mutex */
error = uiomove(sc->buf + off, n, uio);
mtx_unlock(&sc->mtx);
return (error);
```

**Why it matters.** `uiomove(9)` may sleep. When it calls `copyin` or `copyout`, the user page may be paged out, and the kernel may need to page it in from disk, which requires waiting on I/O. A sleep while holding a spin lock (`MTX_SPIN`) deadlocks the system. FreeBSD's `WITNESS` framework panics on this the first time it happens, if `WITNESS` is enabled. On a non-`WITNESS` kernel the result is silent livelock.

The rule is straightforward: spin locks cannot be held across functions that may sleep, and `uiomove` may sleep. Use an `MTX_DEF` mutex (the default, and the one `myfirst` uses) for softc state that is touched by I/O handlers.

### Contrast 5: Resetting Shared State in `d_open`

**Buggy:**

```c
static int
myfirst_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
        struct myfirst_softc *sc = dev->si_drv1;
        /* ... */
        mtx_lock(&sc->mtx);
        sc->bufused = 0;                 /* BAD: wipes other readers' data */
        sc->bufhead = 0;
        mtx_unlock(&sc->mtx);
        /* ... */
}
```

**Correct:**

```c
static int
myfirst_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
        struct myfirst_softc *sc = dev->si_drv1;
        struct myfirst_fh *fh;
        /* ... no shared-state reset ... */
        fh = malloc(sizeof(*fh), M_DEVBUF, M_WAITOK | M_ZERO);
        /* fh starts zeroed, which is correct per-descriptor state */
        /* ... register fh with devfs_set_cdevpriv, bump counters ... */
}
```

**Why it matters.** `d_open` runs once per descriptor. If two readers open the device, the second open will wipe whatever the first open left behind. Driver-wide state (`sc->bufused`, `sc->buf`, counters) belongs to the whole driver, and is reset only at `attach` and `detach`. Per-descriptor state belongs in `fh`, which `malloc(M_ZERO)` initialises to zeros automatically.

A driver that resets shared state in `d_open` looks like it works under a single opener and silently corrupts state when two openers appear. The bug is invisible until the day two users read the device at once.

### Contrast 6: Accounting Before Knowing the Outcome

**Buggy:**

```c
sc->bytes_written += towrite;       /* BAD: count before success */
error = uiomove(sc->buf + tail, towrite, uio);
if (error == 0)
        sc->bufused += towrite;
```

**Correct:**

```c
error = uiomove(sc->buf + tail, towrite, uio);
if (error == 0) {
        sc->bufused += towrite;
        sc->bytes_written += towrite;
}
```

**Why it matters.** If `uiomove` fails part-way through, some bytes may have moved and some may not. The `sc->bytes_written` counter should reflect what actually reached the buffer, not what the driver attempted. Updating counters before the outcome is known makes the counters lie. If a user reads the sysctl to diagnose a problem, they see numbers that do not correspond to reality.

The rule: update counters inside the `if (error == 0)` branch, so success is the only path that increments them. This is a small cost for a large correctness benefit.

### Contrast 7: Dereferencing a User Pointer Directly

**Buggy:**

```c
/* Imagine the driver somehow gets a user pointer, maybe through ioctl. */
static int
handle_user_string(void *user_ptr)
{
        char buf[128];
        memcpy(buf, user_ptr, 128);     /* BAD: user pointer in memcpy */
        /* ... */
}
```

**Correct:**

```c
static int
handle_user_string(void *user_ptr)
{
        char buf[128];
        int error;

        error = copyin(user_ptr, buf, sizeof(buf));
        if (error != 0)
                return (error);
        /* ... */
}
```

**Why it matters.** `memcpy` assumes both pointers refer to memory accessible in the current address space. A user pointer does not. Depending on the platform, the result of passing a user pointer to `memcpy` in kernel context ranges from an `EFAULT`-equivalent fault (on amd64 with SMAP enabled) to silent data corruption (on platforms without user/kernel separation) to an outright kernel panic.

`copyin` and `copyout` are the one-and-only correct way to access user memory from kernel context. They install a fault handler, validate the address, walk page tables safely, and return `EFAULT` on any failure. The performance cost is a few extra instructions; the correctness benefit is "the kernel does not panic when a buggy user program is running".

### Contrast 8: Leaking a Per-Open Structure on `d_open` Failure

**Buggy:**

```c
static int
myfirst_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
        struct myfirst_fh *fh;
        int error;

        fh = malloc(sizeof(*fh), M_DEVBUF, M_WAITOK | M_ZERO);
        /* ... set fields ... */
        error = devfs_set_cdevpriv(fh, myfirst_fh_dtor);
        if (error != 0)
                return (error);         /* BAD: fh is leaked */
        return (0);
}
```

**Correct:**

```c
static int
myfirst_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
        struct myfirst_fh *fh;
        int error;

        fh = malloc(sizeof(*fh), M_DEVBUF, M_WAITOK | M_ZERO);
        /* ... set fields ... */
        error = devfs_set_cdevpriv(fh, myfirst_fh_dtor);
        if (error != 0) {
                free(fh, M_DEVBUF);     /* free before returning */
                return (error);
        }
        return (0);
}
```

**Why it matters.** When `devfs_set_cdevpriv` fails, the kernel does not register the destructor, so the destructor will never run on this `fh`. If the handler returns without freeing `fh`, the memory is leaked. Under steady load, repeated `d_open` failures can leak enough memory to destabilise the kernel.

The rule: in error-unwind paths, every allocation made so far must be freed. The Chapter 8 reader has seen this pattern for attach; it applies equally to `d_open`.

### How to Use This Contrast Table

These eight pairs are not an exhaustive list. They are the bugs we have seen most often in early student drivers, and the bugs the chapter's text has been trying to help you avoid. Read through them once now. Before you write your first real driver outside this book, read them again.

A useful habit while developing: whenever you finish a handler, walk it against the contrast table mentally. Does the handler return a count? Does it special-case zero-resid? Does it have a capacity clamp? Is the mutex type right? Does it reset shared state in `d_open`? Does it account for bytes on failure? Does it dereference any user pointer directly? Does it leak on `d_open` failure? Eight questions, five minutes. The price of the check is small; the cost of shipping one of these bugs into production is large.



## Self-Assessment Before Chapter 10

Chapter 9 has covered a lot of ground. Before you put it down, run through the following checklist. If any item makes you hesitate, the relevant section is worth re-reading before moving on. This is not a test; it is a quick way to identify the spots where your mental model may still be thin.

**Concepts:**

- [ ] I can explain in one sentence what `struct uio` is for.
- [ ] I can name the three fields of `struct uio` my driver reads most often.
- [ ] I can explain why `uiomove(9)` is preferred over `copyin` / `copyout` inside `d_read` and `d_write`.
- [ ] I can explain why `memcpy` across the user / kernel boundary is unsafe.
- [ ] I can explain the difference between `ENXIO`, `EAGAIN`, `ENOSPC`, and `EFAULT` in driver terms.

**Mechanics:**

- [ ] I can write a minimal `d_read` handler that serves a fixed buffer using `uiomove_frombuf(9)`.
- [ ] I can write a minimal `d_write` handler that appends to a kernel buffer with a correct capacity clamp.
- [ ] I know where to put the mutex acquire and release around the transfer.
- [ ] I know how to propagate an errno from `uiomove` back to user space.
- [ ] I know how to mark a write as fully consumed with `uio_resid = 0`.

**Observability:**

- [ ] I can read `sysctl dev.myfirst.0.stats` and interpret each counter.
- [ ] I can spot a memory leak with `vmstat -m | grep devbuf`.
- [ ] I can use `truss(1)` to see what syscalls my test program makes.
- [ ] I can use `fstat(1)` or `fuser(8)` to find who is holding a descriptor.

**Traps:**

- [ ] I would not return a byte count from `d_read` / `d_write`.
- [ ] I would not reject a zero-length request with `EINVAL`.
- [ ] I would not reset `sc->bufused` inside `d_open`.
- [ ] I would not hold a spin lock across a `uiomove` call.

Any "no" here is a signal, not a verdict. Re-read the relevant section; run a small experiment in your lab; come back to the checklist. By the time every box is ticked, you are solidly ready for Chapter 10.



## Wrapping Up

You have just implemented the entry points that make a driver alive. At the end of Chapter 7 your driver had a skeleton. At the end of Chapter 8 it had a well-shaped door. Now, at the end of Chapter 9, data flows through the door in both directions.

The central lesson of the chapter is shorter than it looks. Every `d_read` you will ever write has the same three-line spine: get per-open state, verify liveness, call `uiomove`. Every `d_write` you will ever write has a similar spine with one extra decision (how much room do I have?) and a clamp (`MIN(uio_resid, avail)`) that prevents buffer overruns. Everything else in the chapter is context: why `struct uio` looks the way it does, why `uiomove` is the only safe mover, why errno values matter, why counters matter, why the buffer has to be freed on every error path.

### The Three Ideas That Matter Most

**First, `struct uio` is the contract between your driver and the kernel's I/O machinery.** It carries everything your handler needs to know about a call: what the user asked for, where the user's memory is, what direction the transfer should move, and how much progress has been made. You do not need to memorise all seven fields. You need to recognise `uio_resid` (the remaining work), `uio_offset` (the position, if you care), and `uio_rw` (the direction), and you need to trust `uiomove(9)` with the rest.

**Second, `uiomove(9)` is the boundary between user memory and kernel memory.** Everything your driver ever moves between the two passes through it (or through one of its close relatives: `uiomove_frombuf`, `copyin`, `copyout`). This is not a suggestion. Direct pointer access across the trust boundary either corrupts memory or leaks information, and the kernel has no cheap way to catch the mistake before it becomes a CVE. If a pointer came from user space, route it through the kernel's trust-boundary functions. Always.

**Third, a correct handler is usually a short one.** If your `d_read` or `d_write` is longer than fifteen lines, something is probably wrong. Longer handlers either duplicate logic that belongs elsewhere (in the buffer management, in the per-open state setup, in the sysctls), or they are trying to do something the driver should not be doing in a data-path handler (typically, something that belongs in `d_ioctl`). Keep the handlers short. Put the machinery they call into well-named helper functions. Your future self will thank you.

### The Shape of the Driver You End the Chapter With

Your Stage 3 `myfirst` is a small, honest, in-memory FIFO. The salient features:

- A 4 KiB kernel buffer, allocated in `attach` and freed in `detach`.
- A per-instance mutex guarding `bufhead`, `bufused`, and the associated counters.
- A `d_read` that drains the buffer and advances `bufhead`, collapsing to zero when the buffer empties.
- A `d_write` that appends to the buffer and returns `ENOSPC` when it fills.
- Per-descriptor counters stored in `struct myfirst_fh`, allocated in `d_open`, freed in the destructor.
- A sysctl tree exposing the live driver state.
- Clean `attach` error unwind and clean `detach` ordering.

That shape will come back, recognisable, in half the drivers you will read in Part 4 and Part 6. It is a general pattern, not a one-off demo.

### What You Should Practice Before Starting Chapter 10

Five exercises, in rough order of increasing challenge:

1. Rebuild all three stages from scratch, without looking at the companion tree. Compare your result to the tree afterward; the differences are what you have left to internalise.
2. Introduce an intentional bug in Stage 3: forget to reset `bufhead` when `bufused` reaches zero. Observe what happens on the second big write. Explain the symptom in terms of the code.
3. Add a sysctl that exposes `sc->buflen`. Make it read-only. Then convert it into a tunable that can be set at load time via `kenv` or `loader.conf` and picked up in `attach`. (Chapter 10 revisits tunables formally; this is a preview.)
4. Write a shell script that writes random data of a known length to `/dev/myfirst/0` and then reads it back through `sha256`. Compare the hashes. Do the hashes match even when the write size exceeds the buffer? (They should not; think about why.)
5. Find a driver under `/usr/src/sys/dev` that implements both `d_read` and `d_write`. Read its handlers. Map them against the patterns in this chapter. Good candidates: `/usr/src/sys/dev/null/null.c` (you already know it), `/usr/src/sys/dev/random/randomdev.c`, `/usr/src/sys/dev/speaker/spkr.c`.

### Looking Ahead to Chapter 10

Chapter 10 takes the Stage 3 driver and makes it scale. Four new capabilities show up:

- **A circular buffer** replaces the linear buffer. Writes and reads can both happen continuously without the explicit collapse that Stage 3 uses.
- **Blocking reads** arrive. A reader that calls `read(2)` on an empty buffer can sleep until data is available, rather than returning zero bytes immediately. The kernel's `msleep(9)` is the primitive; the `d_purge` handler is the teardown safety net.
- **Non-blocking I/O** becomes a first-class feature. `O_NONBLOCK` users get `EAGAIN` where a blocking caller would sleep.
- **`poll(2)` and `kqueue(9)` integration**. A user program can wait for the device to become readable or writable without actively attempting the operation. This is the standard way to integrate a device into an event loop.

All four of these build on the same `d_read` / `d_write` shapes you just implemented. You will extend the handlers rather than rewriting them, and the per-descriptor state you have in place will carry the necessary bookkeeping. The chapter before that (this one) is the one where the I/O path itself is correct. Chapter 10 is where the I/O path becomes efficient.

Before you close the file, a last reassurance. The material in this chapter is not as difficult as it may feel on a first read. The pattern is small. The ideas are real, but they are finite, and you have just exercised every one of them against working code. When you read a real driver's `d_read` or `d_write` in the tree, you will now recognise what the function is doing and why. You are not a beginner at this any more. You are an apprentice with a real tool in your hands.



## Reference: The Signatures and Helpers Used in This Chapter

A consolidated reference for the declarations, helpers, and constants the chapter leans on. Keep this page bookmarked while you write drivers; most beginner questions are a lookup in one of these tables.

### `d_read` and `d_write` Signatures

From `/usr/src/sys/sys/conf.h`:

```c
typedef int d_read_t(struct cdev *dev, struct uio *uio, int ioflag);
typedef int d_write_t(struct cdev *dev, struct uio *uio, int ioflag);
```

The return value is zero on success, a positive errno on failure. The byte count is computed from the change in `uio->uio_resid` and reported to user space as the return value of `read(2)` / `write(2)`.

### The Canonical `struct uio`

From `/usr/src/sys/sys/uio.h`:

```c
struct uio {
        struct  iovec *uio_iov;         /* scatter/gather list */
        int     uio_iovcnt;             /* length of scatter/gather list */
        off_t   uio_offset;             /* offset in target object */
        ssize_t uio_resid;              /* remaining bytes to process */
        enum    uio_seg uio_segflg;     /* address space */
        enum    uio_rw uio_rw;          /* operation */
        struct  thread *uio_td;         /* owner */
};
```

### The `uio_seg` and `uio_rw` Enumerations

From `/usr/src/sys/sys/_uio.h`:

```c
enum uio_rw  { UIO_READ, UIO_WRITE };
enum uio_seg { UIO_USERSPACE, UIO_SYSSPACE, UIO_NOCOPY };
```

### `uiomove` Family

From `/usr/src/sys/sys/uio.h`:

```c
int uiomove(void *cp, int n, struct uio *uio);
int uiomove_frombuf(void *buf, int buflen, struct uio *uio);
int uiomove_fromphys(struct vm_page *ma[], vm_offset_t offset, int n,
                     struct uio *uio);
int uiomove_nofault(void *cp, int n, struct uio *uio);
int uiomove_object(struct vm_object *obj, off_t obj_size, struct uio *uio);
```

In beginner driver code, only `uiomove` and `uiomove_frombuf` are common. The others support specific kernel subsystems (physical-page I/O, page-fault-free copies, VM-backed objects) and are out of scope for this chapter.

### `copyin` and `copyout`

From `/usr/src/sys/sys/systm.h`:

```c
int copyin(const void * __restrict udaddr,
           void * __restrict kaddr, size_t len);
int copyout(const void * __restrict kaddr,
            void * __restrict udaddr, size_t len);
int copyinstr(const void * __restrict udaddr,
              void * __restrict kaddr, size_t len,
              size_t * __restrict lencopied);
```

Use these in control paths (`d_ioctl`) where a user pointer arrives outside the uio abstraction. Inside `d_read` and `d_write`, prefer `uiomove`.

### `ioflag` Bits That Matter for Character Devices

From `/usr/src/sys/sys/vnode.h`:

```c
#define IO_NDELAY       0x0004  /* FNDELAY flag set in file table */
```

Set when the descriptor is in non-blocking mode. Your `d_read` or `d_write` can use this to decide whether to block (missing flag) or return `EAGAIN` (flag set). Most of the other `IO_*` flags are filesystem-level and irrelevant to character devices.

### Memory Allocation

From `/usr/src/sys/sys/malloc.h`:

```c
void *malloc(size_t size, struct malloc_type *type, int flags);
void  free(void *addr, struct malloc_type *type);
```

Common flags: `M_WAITOK`, `M_NOWAIT`, `M_ZERO`. Common types for drivers: `M_DEVBUF` (generic) or a driver-specific type declared via `MALLOC_DECLARE` / `MALLOC_DEFINE`.

### Per-Open State (Chapter 8 carryover, used here)

From `/usr/src/sys/sys/conf.h`:

```c
int  devfs_set_cdevpriv(void *priv, d_priv_dtor_t *dtr);
int  devfs_get_cdevpriv(void **datap);
void devfs_clear_cdevpriv(void);
```

The pattern is: allocate in `d_open`, register with `devfs_set_cdevpriv`, retrieve in every later handler with `devfs_get_cdevpriv`, clean up in the destructor that `devfs_set_cdevpriv` registered.

### Errno Values Used in This Chapter

| Errno         | Meaning in a driver context                                |
|---------------|------------------------------------------------------------|
| `0`           | Success.                                                    |
| `ENXIO`       | Device not configured (softc missing, not attached).        |
| `EFAULT`      | Bad user address. Usually propagated from `uiomove`.        |
| `EIO`         | Input / output error. Hardware issue.                       |
| `ENOSPC`      | No space left on device. Buffer full.                       |
| `EAGAIN`      | Would block; relevant in non-blocking mode (Chapter 10).    |
| `EINVAL`      | Invalid argument.                                           |
| `EACCES`      | Permission denied at `open(2)`.                             |
| `EPIPE`       | Broken pipe. Not used by `myfirst`.                         |

### Helpful `device_printf(9)` Patterns

```c
device_printf(sc->dev, "open via %s fh=%p\n", devtoname(sc->cdev), fh);
device_printf(sc->dev, "write rejected: buffer full (used=%zu)\n",
    sc->bufused);
device_printf(sc->dev, "read delivered %zd bytes\n",
    (ssize_t)(before - uio->uio_offset));
```

These are written for readability. A line in `dmesg` you have to decode is a line that probably will not be read when it matters.

### The Three Stages at a Glance

| Stage | `d_read`                                             | `d_write`                          |
|-------|------------------------------------------------------|------------------------------------|
| 1     | Serve fixed message via `uiomove_frombuf`            | Discard writes (like `/dev/null`)  |
| 2     | Serve buffer up to `bufused`                         | Append to buffer, `ENOSPC` if full |
| 3     | Drain buffer from `bufhead`, reset on empty          | Append at `bufhead + bufused`, `ENOSPC` if full |

Stage 3 is the foundation Chapter 10 builds on.

### Consolidated File List for the Chapter

Companion files under `examples/part-02/ch09-reading-and-writing/`:

- `stage1-static-message/`: Stage 1 driver source and Makefile.
- `stage2-readwrite/`: Stage 2 driver source and Makefile.
- `stage3-echo/`: Stage 3 driver source and Makefile.
- `userland/rw_myfirst.c`: small C program to exercise read and write round-trips.
- `userland/stress_rw.c`: multi-process stress test for Lab 9.3 and beyond.
- `README.md`: a short map of the companion tree.

Each stage is independent; you can build, load, and exercise any of them without building the others. The Makefiles are identical except for the driver name (always `myfirst`) and optional tuning flags.



## Appendix A: A Closer Look at `uiomove`'s Internal Loop

For readers who want to see exactly what `uiomove(9)` does, this appendix walks through the core loop of `uiomove_faultflag` as it appears in `/usr/src/sys/kern/subr_uio.c`. You do not need to read this to write a driver. It is here because one reading of the loop will clarify every later question you have about uio semantics.

### The Setup

At entry, the function has:

- A kernel pointer `cp` provided by the caller (your driver).
- An integer `n` provided by the caller (the max bytes to move).
- The uio provided by the kernel dispatch.
- A boolean `nofault` indicating whether page faults during the copy should be handled or fatal.

It sanity-checks a few invariants: the direction is `UIO_READ` or `UIO_WRITE`, the owning thread is the current thread when the segment is user-space, and `uio_resid` is non-negative. Any violation is a `KASSERT` and will panic a kernel with `INVARIANTS` enabled.

### The Main Loop

```c
while (n > 0 && uio->uio_resid) {
        iov = uio->uio_iov;
        cnt = iov->iov_len;
        if (cnt == 0) {
                uio->uio_iov++;
                uio->uio_iovcnt--;
                continue;
        }
        if (cnt > n)
                cnt = n;

        switch (uio->uio_segflg) {
        case UIO_USERSPACE:
                switch (uio->uio_rw) {
                case UIO_READ:
                        error = copyout(cp, iov->iov_base, cnt);
                        break;
                case UIO_WRITE:
                        error = copyin(iov->iov_base, cp, cnt);
                        break;
                }
                if (error)
                        goto out;
                break;

        case UIO_SYSSPACE:
                switch (uio->uio_rw) {
                case UIO_READ:
                        bcopy(cp, iov->iov_base, cnt);
                        break;
                case UIO_WRITE:
                        bcopy(iov->iov_base, cp, cnt);
                        break;
                }
                break;
        case UIO_NOCOPY:
                break;
        }
        iov->iov_base = (char *)iov->iov_base + cnt;
        iov->iov_len -= cnt;
        uio->uio_resid -= cnt;
        uio->uio_offset += cnt;
        cp = (char *)cp + cnt;
        n -= cnt;
}
```

Each iteration does one unit of work: copy up to `cnt` bytes (where `cnt` is `MIN(iov->iov_len, n)`) between the current iovec entry and the kernel buffer. The direction is chosen by the two nested `switch` statements. After a successful copy, all the accounting fields advance in lockstep: the iovec entry shrinks by `cnt`, the uio's resid shrinks by `cnt`, the uio's offset grows by `cnt`, the kernel pointer `cp` advances by `cnt`, and the caller's `n` shrinks by `cnt`.

When an iovec entry is fully drained (`cnt == 0` at loop entry), the function advances to the next entry. When the caller's `n` reaches zero or the uio's resid reaches zero, the loop terminates.

If `copyin` or `copyout` returns non-zero, the function jumps to `out` without updating the fields for that iteration, so the partial-copy accounting is consistent: whatever bytes did copy are reflected in `uio_resid`, whatever did not copy is still pending.

### What You Should Take Away

Three invariants fall out of the loop that matter for your driver code.

- **Your call to `uiomove(cp, n, uio)` moves at most `MIN(n, uio->uio_resid)` bytes.** There is no way to ask for more than the uio has room for; the function caps at whichever side is smaller.
- **On a partial transfer, the state is consistent.** `uio_resid` reflects exactly the bytes that did not move. You can make another call and it will pick up correctly.
- **The fault handling is inside the loop, not around it.** A fault during a `copyin` / `copyout` returns `EFAULT` for the remainder; the fields are still consistent.

These three facts are why the three-line spine we keep returning to (`uiomove`, check error, update state) is sufficient. The kernel is doing the complicated work inside the loop; your driver just has to cooperate.



## Appendix B: Why `read(fd, buf, 0)` Is Allowed

A short note on a question that comes up frequently: why does UNIX allow a `read(fd, buf, 0)` or `write(fd, buf, 0)` call at all?

There are two answers, and both are worth knowing.

**The practical answer**: zero-length I/O is a free test. A user program that wants to check whether a descriptor is in a reasonable state can call `read(fd, NULL, 0)` without committing to a real transfer. If the descriptor is broken, the call returns an error. If it is fine, the call returns zero and costs almost nothing.

**The semantic answer**: the UNIX I/O interface uses byte counts consistently, and special-casing zero is more work than allowing it. A call with `count == 0` is a well-defined no-op: the kernel has to do nothing, and can return zero immediately. The alternative, returning `EINVAL` for zero-count calls, would force every user program that computed a count dynamically to guard against the case. That is the kind of change that breaks decades of code for no benefit.

The driver-side consequence, which we noted earlier: your handler must not panic or error on a zero `uio_resid`. The kernel effectively handles the case for you when you go through `uiomove`, which returns zero immediately if there is nothing to move.

If you ever find yourself writing `if (uio->uio_resid == 0) return (EINVAL);` in a driver, stop. That is the wrong answer. Zero-count I/O is valid; return zero.



## Appendix C: A Short Tour of `/dev/zero`'s Read Path

As a closing piece of analysis, it is worth walking through exactly what happens when a user program calls `read(2)` on `/dev/zero`. The driver is `/usr/src/sys/dev/null/null.c` and the handler is `zero_read`. Once you understand this path, you understand everything in Chapter 9.

### From User Space to Kernel Dispatch

The user calls:

```c
ssize_t n = read(fd, buf, 1024);
```

The C library makes the `read` syscall. The kernel looks up `fd` in the calling process's file table, retrieves the `struct file`, identifies its vnode, dispatches the call into devfs.

devfs identifies the cdev associated with the vnode, acquires a reference on it, and calls its `d_read` function pointer (`zero_read`) with the uio the kernel prepared.

### Inside `zero_read`

```c
static int
zero_read(struct cdev *dev __unused, struct uio *uio, int flags __unused)
{
        void *zbuf;
        ssize_t len;
        int error = 0;

        KASSERT(uio->uio_rw == UIO_READ,
            ("Can't be in %s for write", __func__));
        zbuf = __DECONST(void *, zero_region);
        while (uio->uio_resid > 0 && error == 0) {
                len = uio->uio_resid;
                if (len > ZERO_REGION_SIZE)
                        len = ZERO_REGION_SIZE;
                error = uiomove(zbuf, len, uio);
        }
        return (error);
}
```

- Assert that the direction is correct. Good practice; a `KASSERT` costs nothing in production kernels.
- Set `zbuf` to point at `zero_region`, a large pre-allocated zero-filled area.
- Loop: while the caller wants more bytes, determine the transfer size (min of `uio_resid` and the zero region's size), call `uiomove`, accumulate any error.
- Return.

### Inside `uiomove`

For the first iteration, `uiomove` sees `uio_resid = 1024`, `len = 1024` (since `ZERO_REGION_SIZE` is much larger), `uio_segflg = UIO_USERSPACE`, `uio_rw = UIO_READ`. It selects `copyout(zbuf, buf, 1024)`. The kernel performs the copy, handling any page fault on the user buffer. On success, `uio_resid` drops to zero, `uio_offset` grows by 1024, and the iovec is fully consumed.

### Back Up the Stack

`uiomove` returns zero. The loop in `zero_read` sees `uio_resid == 0` and exits. `zero_read` returns zero.

devfs releases its reference on the cdev. The kernel computes the byte count as `1024 - 0 = 1024`. `read(2)` returns 1024 to the user.

The user's buffer now holds 1024 zero bytes.

### What This Tells You About Your Own Driver

Two observations.

First, every data-path decision in `zero_read` is one you are now making too. How large of a chunk to move per iteration; which buffer to read from; how to handle the error from `uiomove`. Your driver's decisions will differ in the specifics (your buffer is not a pre-allocated zero region, your chunk size is not `ZERO_REGION_SIZE`), but the shape is identical.

Second, everything above `zero_read` is kernel machinery you do not have to write. You implement the handler, and the kernel takes care of the syscall, the file-descriptor lookup, the VFS dispatch, the devfs routing, the reference counting, and the fault handling. That is the power of the abstraction: you add your driver's knowledge, and everything else comes for free.

The flip side is that when you write a driver, you are committing to *cooperating* with that machinery. Every invariant that `uiomove` and devfs rely on is now your responsibility to uphold. The chapter has been walking you through those invariants one at a time, by building three small drivers that each exercise a different subset.

By now, the pattern should be familiar.



## Appendix D: Common `read(2)` / `write(2)` Return Values at the User Side

A short cheat sheet for what a user program sees when it talks to your driver. This is not driver code; it is the view from the other side of the trust boundary. Reading it occasionally is the best inoculation against the subtle bugs that arise when the driver does something other than what a well-behaved UNIX program expects.

### `read(2)`

- A positive integer: that many bytes were placed into the caller's buffer. Less than the requested count means a short read; the caller loops.
- Zero: end of file. No more bytes will ever be produced on this descriptor. The caller stops.
- `-1` with `errno = EAGAIN`: non-blocking mode, no data available right now. The caller waits (via `select(2)` / `poll(2)` / `kqueue(2)`) and tries again.
- `-1` with `errno = EINTR`: a signal interrupted the read. The caller usually retries unless the signal handler tells it not to.
- `-1` with `errno = EFAULT`: the buffer pointer was invalid. The caller has a bug.
- `-1` with `errno = ENXIO`: the device is gone. The caller should close the descriptor and give up.
- `-1` with `errno = EIO`: the device reported a hardware error. The caller may retry or report.

### `write(2)`

- A positive integer: that many bytes were accepted. Less than the offered count means a short write; the caller loops with the remainder.
- Zero: theoretically possible, rarely seen in practice. Usually treated the same as a short write of zero bytes.
- `-1` with `errno = EAGAIN`: non-blocking mode, no space right now. The caller waits and retries.
- `-1` with `errno = ENOSPC`: permanently no space. The caller either stops writing or reopens the descriptor.
- `-1` with `errno = EPIPE`: the reader closed. Relevant for pipe-like devices, not for `myfirst`.
- `-1` with `errno = EFAULT`: the buffer pointer was invalid.
- `-1` with `errno = EINTR`: interrupted by a signal. Usually retried.

### What This Means for Your Driver

Two takeaways.

First, `EAGAIN` is how non-blocking callers expect a driver to say "no data / no room right now, come back later". A non-blocking caller that sees `EAGAIN` does not treat it as an error; it waits for a wake-up (usually via `poll(2)`) and retries. Chapter 10 makes this mechanism work for `myfirst`.

Second, `ENOSPC` is how a driver signals a permanent out-of-room condition on a write. It differs from `EAGAIN` in that the caller does not expect retries to succeed soon. For `myfirst` Stage 3 we use `ENOSPC` when the buffer fills and there is no reader actively draining; Chapter 10 will layer `EAGAIN` on top of the same condition for non-blocking readers and writers.

A driver that returns the wrong errno here is almost indistinguishable from a driver that is misbehaving. The cost of getting it right is tiny. The cost of getting it wrong shows up in confused user programs months later.



## Appendix E: The One-Page Cheat Sheet

If you only have five minutes before starting Chapter 10, here is the one-page version of everything above.

**The signatures:**

```c
static int myfirst_read(struct cdev *dev, struct uio *uio, int ioflag);
static int myfirst_write(struct cdev *dev, struct uio *uio, int ioflag);
```

Return zero on success, a positive errno on failure. Never return a byte count.

**The three-line spine for reads:**

```c
error = devfs_get_cdevpriv((void **)&fh);
if (error) return error;
return uiomove_frombuf(sc->buf, sc->buflen, uio);
```

Or, for a dynamic buffer:

```c
mtx_lock(&sc->mtx);
toread = MIN((size_t)uio->uio_resid, sc->bufused);
error = uiomove(sc->buf + offset, toread, uio);
if (error == 0) { /* update state */ }
mtx_unlock(&sc->mtx);
return error;
```

**The three-line spine for writes:**

```c
mtx_lock(&sc->mtx);
avail = sc->buflen - (sc->bufhead + sc->bufused);
if (avail == 0) { mtx_unlock(&sc->mtx); return ENOSPC; }
towrite = MIN((size_t)uio->uio_resid, avail);
error = uiomove(sc->buf + sc->bufhead + sc->bufused, towrite, uio);
if (error == 0) { sc->bufused += towrite; }
mtx_unlock(&sc->mtx);
return error;
```

**What to remember about uio:**

- `uio_resid`: bytes still pending. `uiomove` decrements this.
- `uio_offset`: position, if meaningful. `uiomove` increments this.
- `uio_rw`: direction. Trust `uiomove` to use it.
- Everything else: do not touch.

**What not to do:**

- Do not dereference user pointers directly.
- Do not use `memcpy` / `bcopy` between user and kernel.
- Do not return byte counts.
- Do not reset driver-wide state in `d_open` / `d_close`.
- Do not forget `M_ZERO` on `malloc(9)`.
- Do not hold a spin lock across `uiomove`.

**Errno values:**

- `0`: success.
- `ENXIO`: device not ready.
- `ENOSPC`: buffer full (permanent).
- `EAGAIN`: would block (non-blocking).
- `EFAULT`: from `uiomove`, propagate.
- `EIO`: hardware error.

That is the chapter.



## Chapter Summary

This chapter built the data path. Starting from the Chapter 8 stubs, we implemented `d_read` and `d_write` in three stages, each a complete and loadable driver.

- **Stage 1** exercised `uiomove_frombuf(9)` against a static kernel string, with per-descriptor offset handling that made two concurrent readers' progress independent.
- **Stage 2** introduced a dynamic kernel buffer, a write path that appended into it, and a read path that served out of it. The buffer was sized at attach time, and a full buffer rejected further writes with `ENOSPC`.
- **Stage 3** turned the buffer into a first-in / first-out queue. Reads drained from the head, writes appended to the tail, and the driver collapsed `bufhead` to zero when the buffer emptied.

Along the way we dissected `struct uio` field by field, explained why `uiomove(9)` is the only legitimate way to cross the user / kernel trust boundary in a read or write handler, and built a small vocabulary of errno values that a well-behaved driver uses: `ENXIO`, `EFAULT`, `ENOSPC`, `EAGAIN`, `EIO`. We walked through the internal loop of `uiomove` so that its guarantees feel earned rather than mysterious. And we finished with five labs, six challenges, a troubleshooting guide, and a one-page cheat sheet.

The Stage 3 driver is the waypoint into Chapter 10. It moves bytes correctly. It does not yet move them efficiently: an empty buffer returns zero bytes immediately, a full buffer returns `ENOSPC` immediately, there is no blocking, no `poll(2)` integration, no ring buffer. Chapter 10 fixes all of that, building on exactly the shapes we just drew.

The pattern you just learned repeats. Every character-device I/O handler in `/usr/src/sys/dev` builds on the same three-argument signature, the same `struct uio`, and the same `uiomove(9)` primitive. The differences between drivers are in how they prepare the data, not in how they move it. Now that you recognise the moving machinery, every handler you open becomes legible almost immediately.

You now know enough to read any `d_read` or `d_write` in the FreeBSD tree and understand what it is doing. That is a significant milestone. Take a minute to appreciate it before turning the page.
