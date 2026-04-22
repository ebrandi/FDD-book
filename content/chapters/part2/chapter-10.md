---
title: "Handling Input and Output Efficiently"
description: "Turning a linear in-kernel buffer into a real circular queue: partial I/O, non-blocking reads and writes, mmap, and the groundwork for safe concurrency."
partNumber: 2
partName: "Building Your First Driver"
chapter: 10
lastUpdated: "2026-04-18"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "TBD"
estimatedReadTime: 210
---

# Handling Input and Output Efficiently

## Reader Guidance & Outcomes

Chapter 9 ended with a small but honest driver. Your `myfirst` module attaches as a Newbus device, creates `/dev/myfirst/0` with an alias at `/dev/myfirst`, allocates per-open state, transfers bytes through `uiomove(9)`, and maintains a simple first-in / first-out kernel buffer. You can write data into it, read the same data back, see the byte counters rise in `sysctl dev.myfirst.0.stats`, and watch your per-open destructor run when each descriptor closes. That is a complete, loadable, source-grounded driver, and the three stages you built in Chapter 9 are the foundation Chapter 10 is about to build on.

There is still something unsatisfying about that Stage 3 FIFO, however. It moves bytes correctly, but it does not use the buffer well. Once `bufhead` walks forward, the space it leaves behind is wasted until the buffer empties and `bufhead` collapses back to zero. A steady producer and a matching consumer can exhaust the capacity long before either side has truly run out of work. A full buffer returns `ENOSPC` immediately, even if a reader is a millisecond away from draining half of it. An empty buffer returns zero bytes as a pseudo end-of-file, even if the caller is prepared to wait. None of those behaviours are wrong for a teaching checkpoint, but none of them scale.

This chapter is where we make the I/O path efficient.

Real drivers move bytes in the background of other work. A reader that reaches the end of the current data may want to block until more arrives. A writer that finds the buffer full may want to block until a reader has made room. A non-blocking caller wants a clear `EAGAIN` instead of a polite fiction. A tool like `cat` or `dd` wants to read and write in chunks that match its own buffer size, not the driver's internal constraints. And the kernel wants the driver to do all of this without losing bytes, without corrupting shared state, and without the sprawling tangle of indices and edge cases that beginner drivers so often accumulate.

The way real drivers achieve this is through a handful of disciplined patterns. A **circular buffer** replaces the linear Stage 3 buffer and keeps the whole capacity in play. **Partial I/O** lets `read(2)` and `write(2)` return whatever portion of the request is actually available, instead of all-or-nothing. **Non-blocking mode** lets a well-written caller ask "is there any data yet?" without committing to sleep. And a careful **refactor** converts the buffer from an ad hoc set of fields inside the softc into a small, named abstraction that Chapter 11 can then protect with real synchronization primitives.

All of this sits firmly inside character-device territory. We are still writing a pseudo-device, still reading and writing through `struct uio`, still loading and unloading our module with `kldload(8)` and `kldunload(8)`. What changes is the *shape* of the data plane between the kernel buffer and the user program, and the quality of the promises the driver makes.

### Why This Chapter Earns Its Own Place

It would be possible to shortcut this material. Many tutorials show a circular buffer in ten lines, sprinkle a `mtx_sleep` somewhere, and declare victory. That approach produces code that passes one test and then develops mysterious bugs under load. The mistakes are usually not in the read and write handlers themselves. They are in how the circular buffer wraps, how `uiomove(9)` is called when the live bytes span the physical end of the buffer, how `IO_NDELAY` is interpreted, how `selrecord(9)` and `selwakeup(9)` compose with a non-blocking caller that never calls `poll(2)`, and how partial writes should be reported back to a user-space program that is itself looping.

This chapter works through those details one at a time. The result is a driver you can stress with `dd`, bombard with a producer-consumer pair, inspect through `sysctl`, and hand to Chapter 11 as a stable base for concurrency work.

### Where Chapter 9 Left the Driver

Checkpoint the state you should be working from. If your source tree and lab box match this outline, everything in Chapter 10 will land cleanly. If they do not, go back and bring Stage 3 to the shape below before continuing.

- One Newbus child under `nexus0`, created by `device_identify`.
- A `struct myfirst_softc` sized for the FIFO buffer plus statistics.
- A mutex `sc->mtx` named after the device, protecting softc counters and buffer indices.
- A sysctl tree at `dev.myfirst.0.stats` exposing `attach_ticks`, `open_count`, `active_fhs`, `bytes_read`, `bytes_written`, and the current `bufhead`, `bufused`, and `buflen`.
- A primary cdev at `/dev/myfirst/0` with ownership `root:operator` and mode `0660`.
- An alias cdev at `/dev/myfirst` pointing at the primary.
- Per-open state via `devfs_set_cdevpriv(9)` and a `myfirst_fh_dtor` destructor.
- A linear FIFO buffer of `MYFIRST_BUFSIZE` bytes, with `bufhead` collapsing to zero on empty.
- `d_read` returning zero bytes when `bufused == 0` (our approximation of EOF at this stage).
- `d_write` returning `ENOSPC` when the tail reaches `buflen`.

Chapter 10 takes that driver and replaces the linear FIFO with a true circular buffer. Then it grows the `d_read` and `d_write` handlers to honour partial I/O properly, adds an `O_NONBLOCK`-aware path with `EAGAIN`, wires in a `d_poll` handler so that `select(2)` and `poll(2)` begin to work, and finishes by pulling the buffer logic into a named abstraction ready for locking.

### What You Will Learn

Once you have finished this chapter, you will be able to:

- Explain, in plain language, what buffering buys a driver and where it starts to hurt.
- Design and implement a fixed-size byte-oriented circular buffer in kernel space.
- Reason about wrap-around correctly: detect it, split transfers across it, and keep the `uio` accounting honest while you do so.
- Integrate that circular buffer into the evolving `myfirst` driver without regressing any earlier behaviour.
- Handle partial reads and partial writes the way classical UNIX programs expect.
- Interpret and honour `IO_NDELAY` in your read and write handlers.
- Implement `d_poll` so `select(2)` and `poll(2)` work against `myfirst`.
- Stress-test buffered I/O from user space using `dd(1)`, `cat(1)`, `hexdump(1)`, and a small producer-consumer pair.
- Recognise the read-modify-write hazards the current driver contains, and make the refactors that will let Chapter 11 introduce real locking without restructuring code.
- Read `d_mmap(9)` and understand when a character-device driver wants to let user space map buffers directly, and when it genuinely does not.
- Talk about zero-copy in a way that distinguishes real savings from slogans.
- Recognise readahead and write-coalescing patterns when you see them in a driver, and describe why they matter to throughput.

### What You Will Build

You will take the Stage 3 driver from Chapter 9 through four main stages, plus a short optional fifth stage that adds memory-mapping support.

1. **Stage 1, a standalone circular buffer.** Before touching the kernel, you will build `cbuf.c` and `cbuf.h` in userland, write a handful of small tests against them, and confirm that wrap-around, empty, and full behave the way your mental model says they should. This is the only part of the chapter you can develop entirely in userland, and it will pay for itself when the driver starts failing in ways that would have been caught by a three-line unit test.
2. **Stage 2, a circular-buffer driver.** You will splice the circular buffer into `myfirst` so that `d_read` and `d_write` now drive the new abstraction. `bufhead` becomes `cb_head`, `bufused` becomes `cb_used`, the fields live inside a small `struct cbuf`, and the wrap-around arithmetic is visible in one place. No behaviour visible to user space changes yet, but the driver is immediately better behaved under steady load.
3. **Stage 3, partial I/O and non-blocking support.** You will grow the handlers to honour partial reads and writes correctly, interpret `IO_NDELAY` as `EAGAIN`, and introduce the blocking read path with `mtx_sleep(9)` and `wakeup(9)`. The driver now rewards polite callers with low latency and rewards patient callers with blocking semantics.
4. **Stage 4, poll-aware and refactor-ready.** You will add a `d_poll` handler, wire up a `struct selinfo`, and refactor all buffer access through a tight set of helper functions so that Chapter 11 can drop in a real locking strategy.
5. **Stage 5, memory mapping (optional).** You will add a small `d_mmap` handler so user space can read the buffer through `mmap(2)`. This stage is explored alongside the supplementary topics in Section 8 and the matching lab at the end of the chapter. You can skip it on a first pass without losing the thread; it revisits the same buffer from a different angle.

You will exercise each stage with the base-system tools, with a small `cb_test` userland program you compile in Stage 1, and with two new helpers: `rw_myfirst_nb` (a non-blocking tester) and `producer_consumer` (a fork-based load harness). Each stage lives on disk under a dedicated directory in `examples/part-02/ch10-handling-io-efficiently/`, and the README there mirrors the chapter's checkpoints.

### What This Chapter Does Not Cover

It is worth naming explicitly what we will *not* try to do here. The deeper discussions of these topics belong in later chapters, and dragging them in now would blur the lessons of this one.

- **Real concurrency correctness.** Chapter 10 uses the mutex that already exists in the softc, and it uses `mtx_sleep(9)` with that mutex as the sleep-interlock argument. That is safe by construction. But this chapter does not explore the full space of race conditions, nor does it categorize lock classes, nor does it teach the reader how to prove a piece of shared state is correctly protected. That is Chapter 11's job, and it is the reason the last section here is called "Refactoring and Preparing for Concurrency" rather than "Concurrency in Drivers."
- **`ioctl(2)`.** The driver still does not implement `d_ioctl`. A few clean-up primitives (flush the buffer, query its fill level) would fit nicely under `ioctl`, but Chapter 25 is the right place for that.
- **`kqueue(2)`.** This chapter implements `d_poll` for `select(2)` and `poll(2)`. The companion `d_kqfilter` handler, along with `knlist` machinery and `EVFILT_READ` filters, is introduced later alongside `taskqueue`-driven drivers.
- **Hardware mmap.** We will build a minimal `d_mmap` handler that lets user space map a preallocated kernel buffer as read-only pages, and we will discuss the design decisions and the things this pattern does and does not achieve. We will not venture into `bus_space(9)`, `bus_dmamap_create(9)`, or the `dev_pager` machinery; those are Part 4 and Part 5 material.
- **Real devices with real backpressure.** The backpressure model here is "the buffer has a fixed capacity and blocks or returns `EAGAIN` when full." Storage and network drivers have richer models (watermarks, credit, BIO queues, mbuf chains). Those details belong in their own chapters.

Keeping the chapter inside those lines is how we keep it honest. The material you will learn is enough to write a respectable pseudo-device and to read most character-device drivers in the tree with confidence.

### Estimated Time Investment

- **Reading only**: roughly ninety minutes, maybe two hours if you are pausing at the diagrams.
- **Reading plus typing the four stages**: four to six hours, split into at least two sessions with a reboot or two.
- **Reading plus all labs and challenges**: eight to twelve hours over three sessions. The challenges are genuinely richer than the main labs, and they reward patient work.

As with Chapter 9, give yourself a fresh lab boot at the start. Do not rush the four stages. The value of the sequence is in watching the driver's behaviour change, one pattern at a time, as you add each capability.

### Prerequisites

Before starting this chapter, confirm:

- Your driver source matches the Chapter 9 Stage 3 example under `examples/part-02/ch09-reading-and-writing/stage3-echo/`. If it does not, stop here and bring it up to that shape first. Chapter 10 assumes it.
- Your lab machine is running FreeBSD 14.3 with a matching `/usr/src`. The APIs and file layouts you will see in this chapter align with that release.
- You read Chapter 9 carefully, including Appendix E (the one-page cheat sheet). The "three-line spine" there for reads and writes is exactly what we are about to grow.
- You are comfortable loading and unloading your own module, watching `dmesg`, reading `sysctl dev.myfirst.0.stats`, and reading the output of `truss(1)` or `ktrace(1)` when a test surprises you.

If any of those are shaky, fixing them now is a better use of time than pushing through this chapter.

### How to Get the Most Out of This Chapter

Three habits stay useful.

First, keep `/usr/src/sys/dev/evdev/cdev.c` open in a second terminal. It is one of the cleanest examples in the tree of a character device that implements a ring buffer, blocks callers when the buffer is empty, honours `O_NONBLOCK`, and wakes sleepers through both `wakeup(9)` and the `selinfo` machinery. We will point at it several times.

Second, keep `/usr/src/sys/kern/subr_uio.c` bookmarked. Chapter 9 walked through this file's `uiomove` internals; here we will revisit it when the buffer wrap forces us to split a transfer. Reading the real code reinforces the right mental model.

Third, run your tests under `truss(1)` occasionally, not only under normal shells. Tracing the syscall return values is the fastest way to distinguish a driver that is honouring partial I/O from one that is silently throwing bytes away.

### Roadmap Through the Chapter

The sections in order are:

1. What buffered I/O is and why it matters. Buckets and pipelines, unbuffered vs. buffered patterns, and where each fits in a driver.
2. Creating a circular buffer. The data structure, invariants, wrap-around arithmetic, and a standalone userland implementation you will test before you put it in the kernel.
3. Integrating that circular buffer into `myfirst`. How `d_read` and `d_write` change, how to handle the split transfer around the wrap, and how to log buffer state so debugging does not require guesswork.
4. Partial reads and partial writes. What they are, why they are correct UNIX behaviour, how to report them through `uio_resid`, and the edge cases you must not trip over.
5. Non-blocking I/O. The `IO_NDELAY` flag, its relationship to `O_NONBLOCK`, the `EAGAIN` convention, and the design of a simple blocking read path using `mtx_sleep(9)` and `wakeup(9)`.
6. Testing buffered I/O from user space. A consolidated test kit: `dd`, `cat`, `hexdump`, `truss`, a small non-blocking tester, and a producer-consumer harness that fork a reader and a writer against `/dev/myfirst`.
7. Refactoring and preparing for concurrency. Where the current code is at risk, how to factor the buffer into helpers, and the shape you want to hand to Chapter 11.
8. Three supplementary topics: `d_mmap(9)` as a minimal kernel-memory mapping (the optional Stage 5), zero-copy considerations for pseudo-devices, and the patterns real high-throughput drivers use (readahead on the read side, write coalescing on the write side).
9. Hands-on labs, a set of concrete exercises you should be able to complete directly against the driver.
10. Challenge exercises that stretch the same skills without introducing brand-new foundations.
11. Troubleshooting notes for the classes of bug this chapter's patterns tend to produce.
12. Wrapping Up and a bridge to Chapter 11.

If this is your first pass, read linearly and do the labs in order. If you are revisiting the material to consolidate, each numbered supplementary topic and the troubleshooting section stand alone.

## Section 1: What Buffered I/O Is and Why It Matters

Every driver that moves bytes between user space and a hardware-side or kernel-side data source has to decide *where* those bytes live in between. The Stage 3 driver from Chapter 9 already makes that decision implicitly. The `bufused` bytes that have been written but not yet read are sitting in a kernel buffer. The reader pulls them out from the head; the writer appends new ones at the tail. The `myfirst` driver is, in that sense, already buffered.

What changes in this chapter is not whether you have a buffer. It is *how* the buffer is shaped, *how much* of its capacity you can keep in play at once, and *what promises* the driver makes to its callers about that buffer's behaviour. Before we look at the data structure, it is worth pausing on the difference between unbuffered and buffered I/O at a conceptual level. That contrast will guide every decision the rest of the chapter asks you to make.

### A Plain-Language Definition

In the simplest framing, **unbuffered I/O** means that every `read(2)` or `write(2)` call touches the underlying source or sink directly. There is no intermediate storage that absorbs bursts, no place where the producer can leave bytes for the consumer to pick up later, no way to decouple the rate at which bytes are produced from the rate at which they are consumed. Each call goes all the way down.

**Buffered I/O**, by contrast, places a small region of memory between the producer and the consumer. A writer drops bytes into the buffer; a reader picks them up. As long as the buffer has free space, writers do not need to wait. As long as the buffer has live bytes, readers do not need to wait. The buffer absorbs short-term mismatches between the two sides.

That sounds like a small distinction, but in driver code it is often the difference between something that works under load and something that does not.

It is worth pausing on a small but important wrinkle. The kernel itself buffers at several layers above and below your driver. The C library's `stdio` buffers writes before they reach the `write(2)` syscall. The VFS path buffers I/O on regular files in the buffer cache. The disk drivers down in Part 7 will buffer at the BIO and queue level. When this chapter says "buffered I/O", it means a buffer *inside the driver*, between the user-facing read and write handlers and whatever data source or sink the driver represents. We are not arguing about whether buffering exists at all; we are deciding where to put one more buffer, and what it should do.

### Two Concrete Pictures

Picture an unbuffered pseudo-device first. Imagine a driver whose `d_write` immediately hands every byte to whatever upstream code consumes them. If the consumer is busy, the writer waits. If the consumer is fast, the writer flies through. There is no give in the system. A burst on either side translates directly into pressure on the other.

Now picture a buffered pseudo-device. The same `d_write` deposits bytes into a small buffer. The consumer pulls bytes out at its own pace. A short burst of writes can complete instantly because the buffer absorbs them. A short pause from the consumer does not stall the writer because the buffer holds the backlog. Both sides feel like they are running smoothly even though their rates do not match exactly at every instant.

The buffered case is what most useful drivers look like in practice. It is not magic; the buffer is finite, and once it fills the producer has to wait or back off. But it gives the system a place to tolerate normal variability, and that tolerance is what makes throughput predictable.

### Buckets vs. Pipelines

A useful analogy here is the difference between carrying water in buckets and carrying water through a pipeline.

When you carry water in buckets, every transfer is a discrete event. You walk to the well, fill the bucket, walk back, empty the bucket, walk again. The producer (the well) and the consumer (the cistern) are coupled tightly through your two arms and your walking pace. If you trip, the system stops. If the cistern is busy, you wait at it. If the well is busy, you wait at it. Each handoff requires that both sides be ready at the same instant.

A pipeline replaces that coupling with a length of pipe. Water enters at the well end and exits at the cistern end. The pipe holds some quantity of water in flight at any moment. The producer can pump as long as there is room in the pipe. The consumer can drain as long as there is water in the pipe. Their schedules no longer have to match. They only have to match *on average*.

A driver buffer is exactly that pipe. It is a finite reservoir that decouples writer rate from reader rate, as long as both rates average out to something the buffer's capacity can absorb. The bucket model corresponds to unbuffered I/O. The pipeline model corresponds to buffered I/O. Both are valid in different situations, and a driver writer's job is to know which one to build.

### Performance: System Calls and Context Switches

The performance advantages of buffering inside a driver are real but indirect. They come from three places.

The first place is **system call overhead**. Every `read(2)` or `write(2)` is a transition from user space to kernel space and back. That transition is cheap on a modern processor, but it is not free. A writer that calls `write(2)` once with a thousand bytes pays one transition. A writer that calls `write(2)` a thousand times with one byte each pays a thousand transitions. If the driver buffers internally, callers can comfortably issue larger reads and writes, and the per-syscall overhead becomes a smaller fraction of the total cost.

The second place is **context switch reduction**. A call that has to wait, because nothing is available right now, often results in the calling thread being suspended and another thread being scheduled. Each suspend and resume is more expensive than a system call. A buffer absorbs the brief mismatches that would otherwise force a sleep, and the threads on both sides keep running.

The third place is **batching opportunities**. A driver that knows it has thousands of bytes ready to ship can sometimes hand the whole batch downstream in one operation, where a driver that processes byte by byte would have to do the same setup and teardown work for each transfer. We will not see this directly with the pseudo-device in this chapter, but it is the underlying argument for the read coalescing and write coalescing patterns we will look at later in the chapter.

None of these advantages should be applied blindly. Buffering also adds latency, since a byte may sit in the buffer for some time before the consumer notices. It adds memory cost. It introduces a pair of indices that must be kept consistent under concurrent access. And it forces a set of design decisions about what to do when the buffer fills (block? drop? overwrite?) and what to do when it empties (block? short-read? signal end-of-file?). There is a reason this chapter spends real time on those decisions.

### Buffered Data Transfers in Device Drivers

Where exactly does buffering pay off in a device driver?

The clearest case is a driver whose data source produces bursts. A serial port driver receives characters whenever the UART chip raises an interrupt; if the consumer is not currently reading, those characters need somewhere to live until it does. A keyboard driver collects key events in the interrupt handler and hands them to user space at whatever rate the application is willing to read. A network driver assembles packets in DMA buffers and feeds them to the protocol stack as soon as it can. In each case, the driver needs a place to hold incoming data between the moment it arrives and the moment it can be delivered.

The mirror case is a driver whose data sink absorbs bursts. A graphics driver may queue commands until the GPU is ready to process them. A printer driver may accept a document and dribble it out at the printer's pace. A storage driver may collect write requests and let an elevator algorithm reorder them for the disk. Again, the driver needs a place to hold outgoing data between the moment the user wrote it and the moment the device is ready.

The pseudo-device we are building in this book sits in the middle of those two patterns. There is no real hardware on either end, but the *shape* of the data path mirrors what real drivers do. When you write to `/dev/myfirst`, the bytes land in a buffer the driver owns. When you read, the bytes come out of that same buffer. Once the buffer is circular and the I/O handlers know how to honour partial transfers, you can stress the driver with `dd if=/dev/zero of=/dev/myfirst bs=1m count=10` from one terminal and `dd if=/dev/myfirst of=/dev/null bs=4k` from another, and the driver will behave the same way a real character device behaves under analogous load.

### When to Use Buffered I/O in a Driver

Almost every driver wants some form of buffering. The interesting question is not whether to buffer, but at what *granularity* and with what *backpressure model*.

Granularity is about the size of the buffer relative to the rates on either side. A buffer that is too small fills constantly and forces the writer to wait, defeating the point. A buffer that is too large hides problems for too long, lets memory grow without bound, and increases worst-case latency. The right size depends on what the buffer is for: the buffer of an interactive keyboard driver only needs to hold a handful of recent events; the buffer of a network driver may hold thousands of packets at peak load.

Backpressure is about what to do when the buffer fills (or empties) and the calling pattern does not match what the driver wants. There are three common strategies, each appropriate in a different setting.

The first is **block**. When the buffer is full, the writer waits. When the buffer is empty, the reader waits. This is the classical UNIX semantics, and it is the right default for terminal devices, pipes, and most general-purpose pseudo-devices. We will implement blocking reads (and optional blocking writes) in Section 5.

The second is **drop**. When the buffer is full, the writer drops the byte (or marks an overflow event) and proceeds. When the buffer is empty, the reader sees zero bytes and proceeds. This is the right default for some real-time and high-rate scenarios, where waiting would do more harm than missing data. Loss must be observable, however, or the driver will silently corrupt the stream from the user's perspective.

The third is **overwrite**. When the buffer is full, the writer overwrites the oldest data with new data. When the buffer is empty, the reader sees zero bytes. This is the right default for a circular log of recent events: a `dmesg(8)`-like history where the most recent bytes are always preserved at the cost of the oldest.

The driver in this chapter uses **block** for blocking-mode callers and **EAGAIN** for non-blocking-mode callers, with no overwrite path. That is the most common pattern in the FreeBSD source tree and the easiest to reason about. The other two strategies appear in later chapters when their use cases come up naturally.

### A First Look at the Cost of an Unbuffered Driver

It is worth being concrete about why the Stage 3 driver from Chapter 9 already starts to hurt at scale.

Suppose you are running a `dd` that writes 64-byte blocks at high rate into the driver, and a parallel `dd` that reads 64-byte blocks out of it. With the Stage 3 buffer of 4096 bytes, you have at most 64 in-flight blocks before the writer hits `ENOSPC` and stops. If the reader pauses for any reason (page faulting in its destination buffer, getting preempted by the scheduler, being moved to a different CPU), the writer parks immediately. As soon as the reader resumes and drains a single block, the writer can fit one more. The overall throughput is the *minimum* of what the two halves achieve in lockstep, plus a steady stream of `ENOSPC` errors that user-space programs are not expected to see.

A circular buffer of the same size holds the same number of in-flight blocks but never wastes the trailing capacity. A non-blocking writer that hits a full buffer cleanly receives `EAGAIN` (the conventional "try again later" signal) instead of `ENOSPC` (the conventional "this device is out of room"), so a tool like `dd` can decide whether to retry or back off. A blocking writer that hits a full buffer goes to sleep on a clear condition variable and wakes up the moment a reader frees space. Each of those changes is small. Together they make a driver feel responsive instead of brittle.

### Where We Are Going

You now have the conceptual ground. The rest of the chapter will translate it into code. Section 2 walks through the data structure, with diagrams and a userland implementation you can test before you trust it inside the kernel. Section 3 moves the implementation into the driver, replacing the linear FIFO. Sections 4 and 5 grow the I/O path so that partial transfers and non-blocking semantics work the way users expect. Section 6 builds the test harness you will use through the rest of Part 2 and most of Part 3. Section 7 prepares the code for the locking work that defines Chapter 11.

The order matters. Each section assumes the changes from the previous one are in place. If you skip ahead, you will land in code that does not compile or behaves in surprising ways. As always, the slow path is the fast path.

### Wrapping Up Section 1

We named the difference between unbuffered and buffered I/O, and we named the costs and benefits of each. We picked an analogy (buckets vs. pipeline) that we can keep returning to. We discussed where buffering pays off in driver code, what backpressure strategies are common, and which one we are committing to for the rest of the chapter. And we set the stage for the data structure that powers the whole thing: the circular buffer.

If you are not yet sure which backpressure strategy your driver should use, that is fine. The default we will build, "block in the kernel and `EAGAIN` outside it," is the safe and conventional choice for general-purpose pseudo-devices, and you will have a clean code shape to revisit if you need a different strategy later. We are about to make that buffer real.

## Section 2: Creating a Circular Buffer

A circular buffer is one of those data structures whose idea is older than the operating systems we now use. It shows up in serial chips, in audio sample queues, in network receive paths, in keyboard event queues, in trace buffers, in `dmesg(8)`, in `printf(3)` libraries, and in nearly every other place where one piece of code wants to leave bytes for another piece of code to pick up later. The structure is simple. The implementation is short. The bugs that beginners make in it are predictable. We will build it once, in userland, with care, and then carry the verified version into the driver in Section 3.

### What a Circular Buffer Is

A linear buffer is the simplest thing that could possibly work: a region of memory plus a "next free byte" index. You write into it from the start and you stop when you reach the end. Once it fills, you either grow it, copy it, or stop accepting new data.

A circular buffer (also called a ring buffer) is the same region of memory, but with a twist in how indices behave. There are two indices: the *head*, which points at the next byte to be read, and the *tail*, which points at the next byte to be written. When either index reaches the end of the underlying memory, it wraps back to the start. The buffer is treated as if its first byte were adjacent to its last byte, forming a closed loop.

Two derived counts matter for using the structure correctly. The number of *live* bytes (how many are currently stored) is what readers care about. The number of *free* bytes (how much capacity is unused) is what writers care about. Both counts can be derived from the head and tail, plus the total capacity, with a small piece of arithmetic.

Visually, the structure looks like this when it is partly full and the live region does not wrap:

```text
  +---+---+---+---+---+---+---+---+
  | _ | _ | A | B | C | D | _ | _ |
  +---+---+---+---+---+---+---+---+
            ^               ^
           head           tail

  capacity = 8, used = 4, free = 4
```

After enough writes, the tail catches up to the end of the underlying memory and wraps to the start. Now the live region itself wraps:

```text
  +---+---+---+---+---+---+---+---+
  | F | G | _ | _ | _ | _ | D | E |
  +---+---+---+---+---+---+---+---+
        ^               ^
       tail           head

  capacity = 8, used = 4, free = 4
  live region: head -> end of buffer, then start of buffer -> tail
```

The "live region wraps" case is the one that catches beginners. A naive `bcopy` of the live data treats the buffer as if it were linear; the bytes you copy are not the bytes you wanted. The correct way to handle this case is to perform the transfer in *two pieces*: from `head` to the end of the buffer, and then from the start of the buffer to `tail`. We will codify exactly this pattern in the helpers below.

### Managing Read and Write Pointers

The head and tail pointers (we will call them indices, because they are integer offsets into a fixed-size array) follow simple rules.

When you read `n` bytes, the head advances by `n`, modulo the capacity. When you write `n` bytes, the tail advances by `n`, modulo the capacity. Reads remove bytes; the live count drops by `n`. Writes add bytes; the live count rises by `n`.

The interesting question is how to detect the two boundary conditions: empty and full. With only `head` and `tail`, the structure is *almost* enough on its own. If `head == tail`, the buffer could be empty (no bytes stored) or full (all capacity stored). The two states look identical from the indices alone. Implementations resolve the ambiguity in one of three ways.

The first way is to keep a separate **count** of live bytes. With `used` available, `used == 0` is unambiguously empty and `used == capacity` is unambiguously full. The structure is slightly larger but the code is short and obvious. This is the design we will use in this chapter.

The second way is to **always leave one byte unused**. With this rule, `head == tail` always means empty, and the buffer is full when `(tail + 1) % capacity == head`. The structure is one byte smaller and the code does not need a `used` field, but every transfer involves an off-by-one that is easy to get wrong. This is the design used in some classic embedded code; it is fine, but it offers no real advantage in our setting.

The third way is to use **monotonically increasing indices** that are never reduced mod capacity, and to compute the live count as `tail - head`. Wrap-around is then a function of how you index into the array (`tail % capacity`), not how you advance the pointer. This is the design used by `buf_ring(9)` in the FreeBSD kernel, which uses a 32-bit counter and trusts the wrap to behave. It is elegant, but it complicates atomic operations and debugging. We will not use it; the explicit `used` field is the right tradeoff for a teaching driver.

### Detecting Buffer Full and Buffer Empty

With the explicit `used` count, the boundary checks become trivial:

- **Empty**: `cb->cb_used == 0`. There are no bytes available to read.
- **Full**: `cb->cb_used == cb->cb_size`. There is no space available to write.

The arithmetic for the head and tail indices is also straightforward:

- After reading `n` bytes: `cb->cb_head = (cb->cb_head + n) % cb->cb_size; cb->cb_used -= n;`
- After writing `n` bytes: `cb->cb_used += n;` (the tail is computed when needed as `(cb->cb_head + cb->cb_used) % cb->cb_size`)

We will keep the tail implicit, derived from `head` and `used`. Some implementations track the tail explicitly. Either choice works as long as you commit to it consistently. With `tail` implicit, we never have to update two indices on a single operation, which removes one whole class of bug.

Two helper-derived quantities will appear repeatedly:

- `cb_free(cb) = cb->cb_size - cb->cb_used`: how many bytes can still be written before the buffer is full.
- `cb_tail(cb) = (cb->cb_head + cb->cb_used) % cb->cb_size`: where the next write should land.

Both are pure functions of the head and used count and the capacity. They have no side effects and are safe to call at any time.

### Allocating a Fixed-Size Circular Buffer

The buffer needs three pieces of state and one chunk of backing memory. Here is the structure we will use:

```c
struct cbuf {
        char    *cb_data;       /* backing storage, cb_size bytes */
        size_t   cb_size;       /* total capacity, in bytes */
        size_t   cb_head;       /* index of next byte to read */
        size_t   cb_used;       /* count of live bytes */
};
```

Three lifetime functions cover the basics:

```c
int   cbuf_init(struct cbuf *cb, size_t size);
void  cbuf_destroy(struct cbuf *cb);
void  cbuf_reset(struct cbuf *cb);
```

`cbuf_init` allocates the backing storage, initialises the indices, and returns zero on success or a positive errno on failure. `cbuf_destroy` releases the backing storage and zeros the structure. `cbuf_reset` empties the buffer without freeing memory; both indices return to zero.

Three accessor functions give the rest of the code the boundary information it needs:

```c
size_t cbuf_used(const struct cbuf *cb);
size_t cbuf_free(const struct cbuf *cb);
size_t cbuf_size(const struct cbuf *cb);
```

These are tiny inline-friendly functions. They do not lock anything; the caller is expected to hold whatever synchronization the larger system requires. (In Stage 4 of this chapter, the driver's mutex will provide that synchronization.)

The two interesting functions are the byte-moving primitives:

```c
size_t cbuf_write(struct cbuf *cb, const void *src, size_t n);
size_t cbuf_read(struct cbuf *cb, void *dst, size_t n);
```

`cbuf_write` copies up to `n` bytes from `src` into the buffer and returns the number actually copied. `cbuf_read` copies up to `n` bytes from the buffer into `dst` and returns the number actually copied. Both functions handle the wrap-around case internally. The caller provides a contiguous source or destination; the buffer takes care of splitting the transfer when the live region or free region crosses the end of the underlying storage.

That signature deserves a moment of attention. Notice that the functions return `size_t`, not `int`. They report progress, not error. Returning fewer bytes than requested is *not* an error condition; it is the correct way to express that the buffer was full (for writes) or empty (for reads). This mirrors the way `read(2)` and `write(2)` themselves work: a positive return value smaller than the request is a "partial transfer", not a failure. We will lean on this in Section 4 when we make the driver honour partial I/O properly.

### A Walkthrough of the Wrap-Around

The wrap-around logic is short, but it is worth tracing one example carefully. Suppose the buffer has capacity 8, head is 6, and used is 4. The live bytes are stored at positions 6, 7, 0, 1.

```text
  +---+---+---+---+---+---+---+---+
  | C | D | _ | _ | _ | _ | A | B |
  +---+---+---+---+---+---+---+---+
        ^               ^
       tail           head
  capacity = 8, used = 4, head = 6, tail = (6+4)%8 = 2
```

Now the caller asks for 3 bytes via `cbuf_read`. The function does the following:

1. Compute `n = MIN(3, used) = MIN(3, 4) = 3`. The caller will get up to 3 bytes.
2. Compute `first = MIN(n, capacity - head) = MIN(3, 8 - 6) = 2`. That is the contiguous chunk starting at the head.
3. Copy `first = 2` bytes from `cb_data + 6` into `dst`. Those are A and B.
4. Compute `second = n - first = 1`. That is the part of the transfer that has to come from the start of the buffer.
5. Copy `second = 1` byte from `cb_data + 0` into `dst + 2`. That is C.
6. Advance `cb_head = (6 + 3) % 8 = 1`. Decrement `cb_used` by 3, leaving 1.

The caller's destination now holds A, B, C. The buffer state has D as its only live byte, with `head = 1` and `used = 1`. The next read will return D from position 1.

The same logic applies to `cbuf_write`, with `tail` taking the role of `head`. The function computes `tail = (head + used) % capacity`, then `first = MIN(n, capacity - tail)`, copies `first` bytes from `src` to `cb_data + tail`, then copies the remainder from `src + first` to `cb_data + 0`, and advances `cb_used` by the total written.

There is exactly one step in either function that wraps. Either the destination wraps (in a write) or the source wraps (in a read), but never both. This is the key property that makes the implementation manageable: the buffer's wrap is a property of the internal data, not of the caller's data, so the caller's source and destination are always treated as ordinary contiguous memory.

### Avoiding Overwrites and Data Loss

A common beginner mistake is to make `cbuf_write` overwrite older data when the buffer fills, on the theory that "newer data is more important." That is sometimes the right policy, as we noted in Section 1, but it must be a *deliberate* design choice and it must be visible to the caller, not a silent mutation of state. The conventional default is that `cbuf_write` returns the number of bytes it actually wrote, and the caller is expected to look at the return value.

The same goes for `cbuf_read`: when the buffer is empty, `cbuf_read` returns zero. The caller is expected to interpret zero as "no bytes available right now", not as an error. Coupling that signal with the driver's `EAGAIN` or with a blocking sleep is the job of the I/O handler, not the buffer itself.

If you ever want a circular buffer with overwrite semantics (a `dmesg`-style log, for instance), the cleanest approach is to write a separate `cbuf_overwrite` function and leave `cbuf_write` strict. Two separate names mean two separate intentions, and a future reader of the code does not have to guess which behaviour is in effect.

### Implementing It in Userland

The right way to learn this structure is to type it once in userland and run it through a few small tests, before you ask the kernel to trust it. The same source can then move into the kernel module almost unchanged, except for the allocation and free calls.

Below is the userland source. It lives in `examples/part-02/ch10-handling-io-efficiently/cbuf-userland/`.

`cbuf.h`:

```c
/* cbuf.h: a fixed-size byte-oriented circular buffer. */
#ifndef CBUF_H
#define CBUF_H

#include <stddef.h>

struct cbuf {
        char    *cb_data;
        size_t   cb_size;
        size_t   cb_head;
        size_t   cb_used;
};

int     cbuf_init(struct cbuf *cb, size_t size);
void    cbuf_destroy(struct cbuf *cb);
void    cbuf_reset(struct cbuf *cb);

size_t  cbuf_size(const struct cbuf *cb);
size_t  cbuf_used(const struct cbuf *cb);
size_t  cbuf_free(const struct cbuf *cb);

size_t  cbuf_write(struct cbuf *cb, const void *src, size_t n);
size_t  cbuf_read(struct cbuf *cb, void *dst, size_t n);

#endif /* CBUF_H */
```

`cbuf.c`:

```c
/* cbuf.c: userland implementation of the byte-oriented ring buffer. */
#include "cbuf.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

int
cbuf_init(struct cbuf *cb, size_t size)
{
        if (cb == NULL || size == 0)
                return (EINVAL);
        cb->cb_data = malloc(size);
        if (cb->cb_data == NULL)
                return (ENOMEM);
        cb->cb_size = size;
        cb->cb_head = 0;
        cb->cb_used = 0;
        return (0);
}

void
cbuf_destroy(struct cbuf *cb)
{
        if (cb == NULL)
                return;
        free(cb->cb_data);
        cb->cb_data = NULL;
        cb->cb_size = 0;
        cb->cb_head = 0;
        cb->cb_used = 0;
}

void
cbuf_reset(struct cbuf *cb)
{
        if (cb == NULL)
                return;
        cb->cb_head = 0;
        cb->cb_used = 0;
}

size_t
cbuf_size(const struct cbuf *cb)
{
        return (cb->cb_size);
}

size_t
cbuf_used(const struct cbuf *cb)
{
        return (cb->cb_used);
}

size_t
cbuf_free(const struct cbuf *cb)
{
        return (cb->cb_size - cb->cb_used);
}

size_t
cbuf_write(struct cbuf *cb, const void *src, size_t n)
{
        size_t avail, tail, first, second;

        avail = cbuf_free(cb);
        if (n > avail)
                n = avail;
        if (n == 0)
                return (0);

        tail = (cb->cb_head + cb->cb_used) % cb->cb_size;
        first = MIN(n, cb->cb_size - tail);
        memcpy(cb->cb_data + tail, src, first);
        second = n - first;
        if (second > 0)
                memcpy(cb->cb_data, (const char *)src + first, second);

        cb->cb_used += n;
        return (n);
}

size_t
cbuf_read(struct cbuf *cb, void *dst, size_t n)
{
        size_t first, second;

        if (n > cb->cb_used)
                n = cb->cb_used;
        if (n == 0)
                return (0);

        first = MIN(n, cb->cb_size - cb->cb_head);
        memcpy(dst, cb->cb_data + cb->cb_head, first);
        second = n - first;
        if (second > 0)
                memcpy((char *)dst + first, cb->cb_data, second);

        cb->cb_head = (cb->cb_head + n) % cb->cb_size;
        cb->cb_used -= n;
        return (n);
}
```

Two things in this code are worth noticing.

First, both `cbuf_write` and `cbuf_read` clamp `n` to the available space or live data *before* doing any copying. This is the key to the partial-transfer semantics: the function is happy to do less work than asked, and it tells the caller exactly how much it did. There is no error path for "buffer is full", because that is not an error.

Second, the `second > 0` guard around the second `memcpy` is not strictly necessary (`memcpy(dst, src, 0)` is well-defined and does nothing), but it makes the wrap-around reasoning visible at a glance. A future reader can tell that the second copy is conditional and that the wrap case is handled.

### A Tiny Test Program

The companion `cb_test.c` exercises the structure with a small but meaningful set of cases. It is short enough to read in full:

```c
/* cb_test.c: simple sanity tests for the cbuf userland implementation. */
#include "cbuf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond, msg) \
        do { if (!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); exit(1); } } while (0)

static void
test_basic(void)
{
        struct cbuf cb;
        char in[8] = "ABCDEFGH";
        char out[8] = {0};
        size_t n;

        CHECK(cbuf_init(&cb, 8) == 0, "init");
        CHECK(cbuf_used(&cb) == 0, "init used");
        CHECK(cbuf_free(&cb) == 8, "init free");

        n = cbuf_write(&cb, in, 4);
        CHECK(n == 4, "write 4");
        CHECK(cbuf_used(&cb) == 4, "used after write 4");

        n = cbuf_read(&cb, out, 2);
        CHECK(n == 2, "read 2");
        CHECK(memcmp(out, "AB", 2) == 0, "AB content");
        CHECK(cbuf_used(&cb) == 2, "used after read 2");

        cbuf_destroy(&cb);
        printf("test_basic OK\n");
}

static void
test_wrap(void)
{
        struct cbuf cb;
        char in[8] = "ABCDEFGH";
        char out[8] = {0};
        size_t n;

        CHECK(cbuf_init(&cb, 8) == 0, "init");

        /* Push head forward by writing and reading 6 bytes. */
        n = cbuf_write(&cb, in, 6);
        CHECK(n == 6, "write 6");
        n = cbuf_read(&cb, out, 6);
        CHECK(n == 6, "read 6");

        /* Now write 6 more, which should wrap. */
        n = cbuf_write(&cb, in, 6);
        CHECK(n == 6, "write 6 after wrap");
        CHECK(cbuf_used(&cb) == 6, "used after wrap write");

        /* Read all of it back; should return ABCDEF. */
        memset(out, 0, sizeof(out));
        n = cbuf_read(&cb, out, 6);
        CHECK(n == 6, "read 6 after wrap");
        CHECK(memcmp(out, "ABCDEF", 6) == 0, "content after wrap");
        CHECK(cbuf_used(&cb) == 0, "empty after drain");

        cbuf_destroy(&cb);
        printf("test_wrap OK\n");
}

static void
test_partial(void)
{
        struct cbuf cb;
        char in[8] = "12345678";
        char out[8] = {0};
        size_t n;

        CHECK(cbuf_init(&cb, 4) == 0, "init small");

        n = cbuf_write(&cb, in, 8);
        CHECK(n == 4, "write clamps to free space");
        CHECK(cbuf_used(&cb) == 4, "buffer full");

        n = cbuf_read(&cb, out, 8);
        CHECK(n == 4, "read clamps to live data");
        CHECK(memcmp(out, "1234", 4) == 0, "content of partial");
        CHECK(cbuf_used(&cb) == 0, "buffer empty after partial drain");

        cbuf_destroy(&cb);
        printf("test_partial OK\n");
}

int
main(void)
{
        test_basic();
        test_wrap();
        test_partial();
        printf("all tests OK\n");
        return (0);
}
```

Compile and run with:

```sh
$ cc -Wall -Wextra -o cb_test cbuf.c cb_test.c
$ ./cb_test
test_basic OK
test_wrap OK
test_partial OK
all tests OK
```

The three tests cover the cases that matter: a basic write/read cycle, a write that wraps around the end of the buffer, and a partial transfer that hits the capacity boundary. They are not exhaustive, but they are enough to catch the most common implementation mistakes. The challenge exercises at the end of the chapter ask you to extend them.

### Why Build It in Userland First

It might feel like a detour to write the buffer in userland when you know the driver is what matters. Three reasons make the detour worth it.

First, the kernel is a hostile place to debug. A bug in the kernel buffer can lock the machine, panic the kernel, or quietly corrupt unrelated state. A bug in the same code in userland is just a failed test that prints a friendly message.

Second, the kernel-side and userland-side implementations of this buffer are virtually identical. The only differences are the allocation primitive (`malloc(9)` with `M_DEVBUF` and `M_WAITOK | M_ZERO` versus libc `malloc(3)`) and the free primitive (`free(9)` versus libc `free(3)`). Once the userland version is correct, the kernel version is almost a copy-paste with a small adjustment.

Third, building the buffer once in isolation forces you to think about its API in calm conditions. By the time you are ready to splice it into the driver, you already know what `cbuf_write` returns, what `cbuf_read` returns, what `cbuf_used` means, and how wrap-around is supposed to work. None of that has to be relearned in the middle of a kernel session.

### Things That Can Still Go Wrong

Even with the helpers above, there are a few mistakes worth flagging now so you do not make them in Section 3.

The first is **forgetting to clamp the request to the available space**. If `cbuf_write` is called with `n = 100` on a buffer with `free = 30`, the function returns 30, not 100. Callers must check the return value and act accordingly. The driver's `d_write` will translate this into a partial write by leaving `uio_resid` at the unconsumed amount. We will be very explicit about this in Section 4.

The second is **forgetting that `cbuf_used` and `cbuf_free` can change between two checks**. In single-threaded userland tests this is impossible. In the kernel, a different thread can modify the buffer between any two function calls if no lock is held. Section 3 holds the softc mutex around all buffer access; Section 7 explains why.

The third is **mixing up indices**. Some implementations track the tail explicitly and the count implicitly. Others do the reverse. Both work. Mixing the two on a single buffer does not. Pick one and stick with it. We pick "head and used"; the tail is always derived.

The fourth is **integer wrap of the indices themselves**. With `size_t` and a buffer that is a few thousand bytes, the indices can never exceed `cb_size`, and `(cb_head + n) % cb_size` is always well-defined. If you ever extend this code to a buffer larger than `SIZE_MAX / 2`, that is no longer true; you would need 64-bit indices and explicit modular arithmetic. For our pseudo-device with a 4 KB or 64 KB buffer, the basic structure is more than sufficient.

### Wrapping Up Section 2

You now have a clean, tested, byte-oriented circular buffer. It clamps requests to the available space, reports the actual transfer size, and handles wrap-around in the only place where wrap-around is meaningful: inside the buffer itself. The userland tests give you a small piece of evidence that the implementation behaves the way the diagrams said it should.

Section 3 takes this code into the kernel. The shape stays almost identical; the allocation and synchronization change. By the end of the next section, your driver's `d_read` and `d_write` will be calling `cbuf_read` and `cbuf_write` instead of doing their own arithmetic, and the logic that used to live inline in `myfirst.c` will have a name.

## Section 3: Integrating a Circular Buffer into Your Driver

The userland implementation of `cbuf` is the same code you are about to drop into the kernel. Almost. There are three small changes: the allocator, the deallocator, and a paranoia level that the kernel demands which userland does not. After the splice, the driver's read and write handlers shrink considerably, and the wrap-around arithmetic disappears from `myfirst.c` into the helpers where it belongs.

This section walks through the splice carefully. We will start with the kernel-side variant of the buffer, then move to the integration changes inside `myfirst.c`, and finally look at how to add a few sysctl knobs that make the driver's internal state visible while you are debugging.

### Moving `cbuf` Into the Kernel

The kernel-side header `cbuf.h` is identical to the userland one:

```c
#ifndef CBUF_H
#define CBUF_H

#include <sys/types.h>

struct cbuf {
        char    *cb_data;
        size_t   cb_size;
        size_t   cb_head;
        size_t   cb_used;
};

int     cbuf_init(struct cbuf *cb, size_t size);
void    cbuf_destroy(struct cbuf *cb);
void    cbuf_reset(struct cbuf *cb);

size_t  cbuf_size(const struct cbuf *cb);
size_t  cbuf_used(const struct cbuf *cb);
size_t  cbuf_free(const struct cbuf *cb);

size_t  cbuf_write(struct cbuf *cb, const void *src, size_t n);
size_t  cbuf_read(struct cbuf *cb, void *dst, size_t n);

#endif /* CBUF_H */
```

The kernel-side `cbuf.c` is almost a copy of the userland file with two replacements. `malloc(3)` becomes `malloc(9)` from `M_DEVBUF` with the `M_WAITOK | M_ZERO` flags. `free(3)` becomes `free(9)` from `M_DEVBUF`. The `memcpy(3)` calls remain valid in kernel context: the kernel has its own `memcpy` and `bcopy` symbols. Here is the full kernel version:

```c
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/malloc.h>

#include "cbuf.h"

MALLOC_DEFINE(M_CBUF, "cbuf", "Chapter 10 circular buffer");

int
cbuf_init(struct cbuf *cb, size_t size)
{
        if (cb == NULL || size == 0)
                return (EINVAL);
        cb->cb_data = malloc(size, M_CBUF, M_WAITOK | M_ZERO);
        cb->cb_size = size;
        cb->cb_head = 0;
        cb->cb_used = 0;
        return (0);
}

void
cbuf_destroy(struct cbuf *cb)
{
        if (cb == NULL || cb->cb_data == NULL)
                return;
        free(cb->cb_data, M_CBUF);
        cb->cb_data = NULL;
        cb->cb_size = 0;
        cb->cb_head = 0;
        cb->cb_used = 0;
}

void
cbuf_reset(struct cbuf *cb)
{
        if (cb == NULL)
                return;
        cb->cb_head = 0;
        cb->cb_used = 0;
}

size_t
cbuf_size(const struct cbuf *cb)
{
        return (cb->cb_size);
}

size_t
cbuf_used(const struct cbuf *cb)
{
        return (cb->cb_used);
}

size_t
cbuf_free(const struct cbuf *cb)
{
        return (cb->cb_size - cb->cb_used);
}

size_t
cbuf_write(struct cbuf *cb, const void *src, size_t n)
{
        size_t avail, tail, first, second;

        avail = cbuf_free(cb);
        if (n > avail)
                n = avail;
        if (n == 0)
                return (0);

        tail = (cb->cb_head + cb->cb_used) % cb->cb_size;
        first = MIN(n, cb->cb_size - tail);
        memcpy(cb->cb_data + tail, src, first);
        second = n - first;
        if (second > 0)
                memcpy(cb->cb_data, (const char *)src + first, second);

        cb->cb_used += n;
        return (n);
}

size_t
cbuf_read(struct cbuf *cb, void *dst, size_t n)
{
        size_t first, second;

        if (n > cb->cb_used)
                n = cb->cb_used;
        if (n == 0)
                return (0);

        first = MIN(n, cb->cb_size - cb->cb_head);
        memcpy(dst, cb->cb_data + cb->cb_head, first);
        second = n - first;
        if (second > 0)
                memcpy((char *)dst + first, cb->cb_data, second);

        cb->cb_head = (cb->cb_head + n) % cb->cb_size;
        cb->cb_used -= n;
        return (n);
}
```

Three points are worth a short comment.

The first is `MALLOC_DEFINE(M_CBUF, "cbuf", ...)`. This declares a private memory tag for the buffer's allocations so that `vmstat -m` can show how much memory the cbuf code is using, separately from the rest of the driver. We declare it once, in `cbuf.c`, with internal linkage to the rest of the module. The driver's softc still uses `M_DEVBUF`. The two tags can coexist; they are bookkeeping labels, not pools.

The second is the `M_WAITOK` flag. Because we never call `cbuf_init` from interrupt context (we call it from `myfirst_attach`, which runs in normal kernel thread context during module load), it is safe to wait for memory if the system is briefly low. With `M_WAITOK`, `malloc(9)` will not return `NULL`; if the allocation cannot proceed it will sleep until it can. We therefore do not need to test the result for `NULL`. If we ever want to call `cbuf_init` from a context where sleep is forbidden, we would need to switch to `M_NOWAIT` and handle a possible `NULL`. For Chapter 10's purposes, `M_WAITOK` is the right choice.

The third is that **the kernel `cbuf` does not lock**. It is a pure data structure. The locking strategy is the *caller's* responsibility. Inside `myfirst.c`, we will hold `sc->mtx` across every call into `cbuf`. That keeps the abstraction small and gives Chapter 11 a clean refactor target.

### What Changes in `myfirst.c`

Bring up the Stage 3 file from Chapter 9 in your editor. The integration involves the following changes:

1. Replace the four buffer-related softc fields (`buf`, `buflen`, `bufhead`, `bufused`) with a single `struct cbuf cb` member.
2. Remove the `MYFIRST_BUFSIZE` macro from `myfirst.c` (we keep it but in a single header to avoid duplication).
3. Initialise the buffer in `myfirst_attach` with `cbuf_init`.
4. Tear it down in `myfirst_detach` and on the attach failure path with `cbuf_destroy`.
5. Rewrite `myfirst_read` to call `cbuf_read` against a stack-resident bounce buffer, then `uiomove` the bounce buffer out.
6. Rewrite `myfirst_write` to `uiomove` into a stack-resident bounce buffer and then `cbuf_write` into the ring.

The last two changes deserve a short discussion before we look at the code. Why a bounce buffer? Why not call `uiomove` directly against the cbuf storage?

The answer is that `uiomove` does not understand wrap-around. It expects a contiguous destination (for reads) or a contiguous source (for writes). If the live region of our circular buffer wraps, calling `uiomove(cb->cb_data + cb->cb_head, n, uio)` would copy past the end of the underlying memory and into whatever is allocated next. That is a heap corruption bug waiting to happen. Two safe shapes exist; you can pick either.

The first safe shape is to call `uiomove` *twice*, once for each side of the wrap. The driver computes the contiguous chunk available at `cb->cb_data + cb->cb_head`, calls `uiomove` for that chunk, and then calls `uiomove` again for the wrapped portion at `cb->cb_data + 0`. This is efficient because there is no extra copy. It is also more complex and harder to get right; the driver has to do partial accounting of `uio_resid` between the two `uiomove` calls, and any cancellation in the middle (signal, page fault) leaves the buffer in a partially drained state.

The second safe shape is to use a kernel-side **bounce buffer**: a small temporary on the stack that lives only for the duration of the I/O call. The driver reads bytes out of the cbuf into the bounce buffer with `cbuf_read`, then `uiomove`s the bounce buffer into user space. On the write side, it `uiomove`s from user space into the bounce buffer, then `cbuf_write`s the bounce buffer into the cbuf. The cost is one extra in-kernel copy per chunk; the benefit is simplicity, locality of error handling, and the ability to do all wrap-aware logic inside the cbuf where it belongs.

The bounce buffer approach is what we will use in this chapter. It is the same approach that drivers like `evdev/cdev.c` use (with `bcopy` between the per-client ring and a stack-resident `event` structure, before `uiomove`-ing the structure to user space). The stack-resident bounce is small (256 or 512 bytes is plenty), the loop runs as many times as the user's transfer size demands, and each iteration is independently restartable if `uiomove` fails. The performance cost is negligible for everything except very-high-throughput hardware drivers, and even there the trade-off is usually worth it for the readability gain.

### The Stage 2 Driver: Handlers Refactored

Here is what the relevant pieces of the driver look like after the splice. The full source is in `examples/part-02/ch10-handling-io-efficiently/stage2-circular/myfirst.c`. We will show the I/O handlers inline, then walk through what changed.

```c
#define MYFIRST_BUFSIZE         4096
#define MYFIRST_BOUNCE          256

struct myfirst_softc {
        device_t                dev;
        int                     unit;

        struct mtx              mtx;

        uint64_t                attach_ticks;
        uint64_t                open_count;
        uint64_t                bytes_read;
        uint64_t                bytes_written;

        int                     active_fhs;
        int                     is_attached;

        struct cbuf             cb;

        struct cdev            *cdev;
        struct cdev            *cdev_alias;

        struct sysctl_ctx_list  sysctl_ctx;
        struct sysctl_oid      *sysctl_tree;
};

static int
myfirst_read(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct myfirst_softc *sc = dev->si_drv1;
        struct myfirst_fh *fh;
        char bounce[MYFIRST_BOUNCE];
        size_t take, got;
        int error;

        error = devfs_get_cdevpriv((void **)&fh);
        if (error != 0)
                return (error);
        if (sc == NULL || !sc->is_attached)
                return (ENXIO);

        while (uio->uio_resid > 0) {
                mtx_lock(&sc->mtx);
                take = MIN((size_t)uio->uio_resid, sizeof(bounce));
                got = cbuf_read(&sc->cb, bounce, take);
                if (got == 0) {
                        mtx_unlock(&sc->mtx);
                        break;          /* empty: short read or EOF */
                }
                sc->bytes_read += got;
                fh->reads += got;
                mtx_unlock(&sc->mtx);

                error = uiomove(bounce, got, uio);
                if (error != 0)
                        return (error);
        }
        return (0);
}

static int
myfirst_write(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct myfirst_softc *sc = dev->si_drv1;
        struct myfirst_fh *fh;
        char bounce[MYFIRST_BOUNCE];
        size_t want, put, room;
        int error;

        error = devfs_get_cdevpriv((void **)&fh);
        if (error != 0)
                return (error);
        if (sc == NULL || !sc->is_attached)
                return (ENXIO);

        while (uio->uio_resid > 0) {
                mtx_lock(&sc->mtx);
                room = cbuf_free(&sc->cb);
                mtx_unlock(&sc->mtx);
                if (room == 0)
                        break;          /* full: short write */

                want = MIN((size_t)uio->uio_resid, sizeof(bounce));
                want = MIN(want, room);
                error = uiomove(bounce, want, uio);
                if (error != 0)
                        return (error);

                mtx_lock(&sc->mtx);
                put = cbuf_write(&sc->cb, bounce, want);
                sc->bytes_written += put;
                fh->writes += put;
                mtx_unlock(&sc->mtx);

                /*
                 * cbuf_write may store less than 'want' if another
                 * writer slipped in between our snapshot of 'room'
                 * and our cbuf_write call and consumed some of the
                 * space we had sized ourselves against.  With a single
                 * writer that cannot happen and put == want always.
                 * We still handle it defensively: a serious driver
                 * would reserve space up front to avoid losing bytes,
                 * and Chapter 11 will revisit this with proper
                 * multi-writer synchronization.
                 */
                if (put < want) {
                        /*
                         * The 'want - put' bytes we copied into 'bounce'
                         * with uiomove have already left the caller's
                         * uio and cannot be pushed back.  Record the
                         * loss by breaking out of the loop; the kernel
                         * will report the bytes actually stored via
                         * uio_resid.  This path is only reachable under
                         * concurrent writers, which the design here
                         * does not yet handle.
                         */
                        break;
                }
        }
        return (0);
}
```

A few things changed from Stage 3 of Chapter 9.

The first change is the **loop**. Both handlers now loop until `uio_resid` reaches zero or until the buffer cannot satisfy the next iteration. Each iteration moves at most `sizeof(bounce)` bytes, which is the size of the stack bounce. For a small request, the loop runs once. For a large request, it runs as many times as needed. This is what makes partial I/O work cleanly: the handlers naturally produce a short read or write when the buffer reaches a boundary.

The second change is that **all buffer access is bracketed by `mtx_lock`/`mtx_unlock`**. The `cbuf` data structure is unaware of locking; the driver provides it. We hold the lock around every `cbuf_*` call and around every update of the byte counters. We do *not* hold the lock across `uiomove(9)`. Holding a mutex across `uiomove` is a real bug in FreeBSD: `uiomove` may sleep on a page fault, and sleeping with a mutex held is a sleep-with-mutex panic. The Chapter 9 walkthrough discussed this; we are now operationalising the rule by separating the cbuf access (under lock) from the uiomove (without lock).

The third change is that the **read handler returns 0** when the buffer is empty, after possibly having transferred some bytes. The old Stage 3 behaviour was identical at this layer. What changes is that the *next* section makes it possible for the read to block instead, and the section after that adds an `EAGAIN` path for non-blocking callers. The structure here is the foundation for both extensions.

The fourth change is that the **write handler honours partial writes**. When `cbuf_free(&sc->cb)` returns zero, the loop exits and the handler returns 0 with `uio_resid` reflecting the bytes that were not consumed. The user-space `write(2)` call will see a short write count, which is the conventional UNIX way to say "I accepted this many of your bytes; please call me again with the rest later." Section 4 talks at length about why this matters and how to write user code that handles it.

### Updating `attach` and `detach`

The lifecycle changes are small but real:

```c
static int
myfirst_attach(device_t dev)
{
        struct myfirst_softc *sc;
        struct make_dev_args args;
        int error;

        sc = device_get_softc(dev);
        sc->dev = dev;
        sc->unit = device_get_unit(dev);

        mtx_init(&sc->mtx, device_get_nameunit(dev), "myfirst", MTX_DEF);

        sc->attach_ticks = ticks;
        sc->is_attached = 1;
        sc->active_fhs = 0;
        sc->open_count = 0;
        sc->bytes_read = 0;
        sc->bytes_written = 0;

        error = cbuf_init(&sc->cb, MYFIRST_BUFSIZE);
        if (error != 0)
                goto fail_mtx;

        make_dev_args_init(&args);
        args.mda_devsw = &myfirst_cdevsw;
        args.mda_uid = UID_ROOT;
        args.mda_gid = GID_OPERATOR;
        args.mda_mode = 0660;
        args.mda_si_drv1 = sc;

        error = make_dev_s(&args, &sc->cdev, "myfirst/%d", sc->unit);
        if (error != 0)
                goto fail_cb;

        sc->cdev_alias = make_dev_alias(sc->cdev, "myfirst");
        if (sc->cdev_alias == NULL)
                device_printf(dev, "failed to create /dev/myfirst alias\n");

        sysctl_ctx_init(&sc->sysctl_ctx);
        sc->sysctl_tree = SYSCTL_ADD_NODE(&sc->sysctl_ctx,
            SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
            OID_AUTO, "stats", CTLFLAG_RD | CTLFLAG_MPSAFE, 0,
            "Driver statistics");

        SYSCTL_ADD_U64(&sc->sysctl_ctx, SYSCTL_CHILDREN(sc->sysctl_tree),
            OID_AUTO, "attach_ticks", CTLFLAG_RD,
            &sc->attach_ticks, 0, "Tick count when driver attached");
        SYSCTL_ADD_U64(&sc->sysctl_ctx, SYSCTL_CHILDREN(sc->sysctl_tree),
            OID_AUTO, "open_count", CTLFLAG_RD,
            &sc->open_count, 0, "Lifetime number of opens");
        SYSCTL_ADD_INT(&sc->sysctl_ctx, SYSCTL_CHILDREN(sc->sysctl_tree),
            OID_AUTO, "active_fhs", CTLFLAG_RD,
            &sc->active_fhs, 0, "Currently open descriptors");
        SYSCTL_ADD_U64(&sc->sysctl_ctx, SYSCTL_CHILDREN(sc->sysctl_tree),
            OID_AUTO, "bytes_read", CTLFLAG_RD,
            &sc->bytes_read, 0, "Total bytes drained from the FIFO");
        SYSCTL_ADD_U64(&sc->sysctl_ctx, SYSCTL_CHILDREN(sc->sysctl_tree),
            OID_AUTO, "bytes_written", CTLFLAG_RD,
            &sc->bytes_written, 0, "Total bytes appended to the FIFO");
        SYSCTL_ADD_PROC(&sc->sysctl_ctx, SYSCTL_CHILDREN(sc->sysctl_tree),
            OID_AUTO, "cb_used",
            CTLTYPE_UINT | CTLFLAG_RD | CTLFLAG_MPSAFE,
            sc, 0, myfirst_sysctl_cb_used, "IU",
            "Live bytes currently held in the circular buffer");
        SYSCTL_ADD_PROC(&sc->sysctl_ctx, SYSCTL_CHILDREN(sc->sysctl_tree),
            OID_AUTO, "cb_free",
            CTLTYPE_UINT | CTLFLAG_RD | CTLFLAG_MPSAFE,
            sc, 0, myfirst_sysctl_cb_free, "IU",
            "Free bytes available in the circular buffer");
        SYSCTL_ADD_UINT(&sc->sysctl_ctx, SYSCTL_CHILDREN(sc->sysctl_tree),
            OID_AUTO, "cb_size", CTLFLAG_RD,
            (unsigned int *)&sc->cb.cb_size, 0,
            "Capacity of the circular buffer");

        device_printf(dev,
            "Attached; node /dev/%s (alias /dev/myfirst), cbuf=%zu bytes\n",
            devtoname(sc->cdev), cbuf_size(&sc->cb));
        return (0);

fail_cb:
        cbuf_destroy(&sc->cb);
fail_mtx:
        mtx_destroy(&sc->mtx);
        sc->is_attached = 0;
        return (error);
}

static int
myfirst_detach(device_t dev)
{
        struct myfirst_softc *sc;

        sc = device_get_softc(dev);

        mtx_lock(&sc->mtx);
        if (sc->active_fhs > 0) {
                mtx_unlock(&sc->mtx);
                device_printf(dev,
                    "Cannot detach: %d open descriptor(s)\n",
                    sc->active_fhs);
                return (EBUSY);
        }
        mtx_unlock(&sc->mtx);

        if (sc->cdev_alias != NULL) {
                destroy_dev(sc->cdev_alias);
                sc->cdev_alias = NULL;
        }
        if (sc->cdev != NULL) {
                destroy_dev(sc->cdev);
                sc->cdev = NULL;
        }
        sysctl_ctx_free(&sc->sysctl_ctx);
        cbuf_destroy(&sc->cb);
        mtx_destroy(&sc->mtx);
        sc->is_attached = 0;
        return (0);
}
```

The two new sysctl handlers are short:

```c
static int
myfirst_sysctl_cb_used(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        unsigned int val;

        mtx_lock(&sc->mtx);
        val = (unsigned int)cbuf_used(&sc->cb);
        mtx_unlock(&sc->mtx);
        return (sysctl_handle_int(oidp, &val, 0, req));
}

static int
myfirst_sysctl_cb_free(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        unsigned int val;

        mtx_lock(&sc->mtx);
        val = (unsigned int)cbuf_free(&sc->cb);
        mtx_unlock(&sc->mtx);
        return (sysctl_handle_int(oidp, &val, 0, req));
}
```

The handlers exist because we want a *consistent* snapshot of the buffer state when the user reads `sysctl dev.myfirst.0.stats.cb_used`. Reading the field directly (the way Stage 3 did with `bufused`) is racy: a concurrent write could be modifying it as `sysctl(8)` reads it, producing a torn value. The handler holds the mutex around the read, so the value the user sees is at least *self-consistent* (it represents the buffer state at one moment in time, not a half-update). The buffer can change immediately after the handler releases the lock, of course; that is fine, because by the time `sysctl(8)` formats and prints the number, the buffer has often changed anyway. What we are preventing is a read of a partially modified field, not a stale read.

### Logging Buffer State for Debugging

When the driver misbehaves, the first question is almost always "what was the buffer doing?" Adding a small amount of `device_printf` traffic to the I/O handlers, behind a sysctl-controlled debug flag, makes this question easy to answer. Here is the pattern:

```c
static int myfirst_debug = 0;
SYSCTL_INT(_dev_myfirst, OID_AUTO, debug, CTLFLAG_RW,
    &myfirst_debug, 0, "Verbose I/O tracing for the myfirst driver");

#define MYFIRST_DBG(sc, fmt, ...) do {                                  \
        if (myfirst_debug)                                              \
                device_printf((sc)->dev, fmt, ##__VA_ARGS__);           \
} while (0)
```

Then in the I/O handlers, call `MYFIRST_DBG(sc, "read got=%zu used=%zu free=%zu\n", got, cbuf_used(&sc->cb), cbuf_free(&sc->cb));` after a successful `cbuf_read`. With `myfirst_debug` set to 0, the macro reduces to a no-op and the production path is untouched. With `sysctl dev.myfirst.debug=1`, every transfer prints a one-line trace into `dmesg`, which is invaluable when the driver is doing something you do not understand.

Be polite about how much trace you emit. A single log line per transfer is fine. A log line per byte transferred would melt `dmesg`'s ring buffer in seconds and would change the timing of the driver enough to hide some bugs. The pattern above logs once per `cbuf_read` or `cbuf_write` call, which is once per loop iteration, which is once per chunk of up to 256 bytes. That is roughly the right granularity.

Finally, remember to set `myfirst_debug = 0` before you load the driver in production. The line is there as a development aid, not as a permanent feature.

### Why the `cbuf` Has Its Own Memory Tag

When you run `vmstat -m` on a FreeBSD system, you see a long list of memory tags and how much memory each one currently holds. Tags are an essential observability tool: if memory is leaking somewhere in the kernel, the tag whose count keeps growing tells you where to look. We gave `cbuf` its own tag (`M_CBUF`) so that its allocations are visible separately from the rest of the driver's allocations.

To see the effect, load the Stage 2 driver and run:

```sh
$ vmstat -m | head -1
         Type InUse MemUse Requests  Size(s)
$ vmstat -m | grep -E '(^\s+Type|cbuf|myfirst)'
         Type InUse MemUse Requests  Size(s)
         cbuf     1      4K        1  4096
```

The four kilobytes correspond to the single 4 KB allocation `cbuf_init` made for `sc->cb.cb_data`. Unload the driver and the count drops back to zero. If at any point the count ever rises *without* a corresponding driver attach, you have a leak in `cbuf_init` or `cbuf_destroy`. This is the kind of regression that would otherwise be invisible until the system ran out of memory hours later.

### A Quick Trace of an Aligned and a Wrapped Transfer

To make the wrap-around behaviour real, let us trace two writes through the Stage 2 driver. Assume the buffer is empty at the start, capacity is 4096, and a user calls `write(fd, buf, 100)` followed later by `write(fd, buf2, 100)`.

The first write goes through `myfirst_write`:

1. `uio_resid = 100`, `cbuf_free = 4096`, `room = 4096`.
2. Loop iteration 1: `want = MIN(100, 256, 4096) = 100`. `uiomove` copies 100 bytes from user space into `bounce`. `cbuf_write(&sc->cb, bounce, 100)` returns 100, advances `cb_used` to 100, leaves `cb_head = 0`. The implicit tail is now 100.
3. `uio_resid = 0`. The loop exits. The handler returns 0. The user sees a write count of 100.

The buffer state is: `cb_data[0..99]` holds the data, `cb_head = 0`, `cb_used = 100`, `cb_size = 4096`.

Now the second write arrives. Before it does, suppose a reader has consumed 80 bytes, leaving `cb_head = 80`, `cb_used = 20`. The implicit tail is at position 100. `myfirst_write` runs:

1. `uio_resid = 100`, `cbuf_free = 4076`, `room = 4076`.
2. Loop iteration 1: `want = MIN(100, 256, 4076) = 100`. `uiomove` copies 100 bytes from user space into `bounce`. `cbuf_write(&sc->cb, bounce, 100)` advances the implicit tail from 100 to 200, sets `cb_used = 120`, returns 100.
3. `uio_resid = 0`. The handler returns 0. The user sees a write count of 100.

Both transfers were "aligned" in the sense that neither crossed the end of the underlying buffer. Now imagine a much later state where `cb_head = 4000` and `cb_used = 80`. The live bytes occupy positions 4000..4079, and the implicit tail is 4080. Capacity is 4096. The free space is 4016 bytes, but it splits across the wrap: 16 bytes contiguous after position 4080, then 4000 contiguous from position 0.

A user calls `write(fd, buf, 64)`:

1. `uio_resid = 64`, `cbuf_free = 4016`, `room = 4016`.
2. Loop iteration 1: `want = MIN(64, 256, 4016) = 64`. `uiomove` copies 64 bytes into `bounce`. `cbuf_write(&sc->cb, bounce, 64)` runs:
   - `tail = (4000 + 80) % 4096 = 4080`.
   - `first = MIN(64, 4096 - 4080) = 16`. Copies 16 bytes from `bounce + 0` to `cb_data + 4080`.
   - `second = 64 - 16 = 48`. Copies 48 bytes from `bounce + 16` to `cb_data + 0`.
   - `cb_used += 64`, becoming 144.
3. `uio_resid = 0`. Handler returns 0.

The wrap was handled inside `cbuf_write` and was invisible to the driver. That is the whole point of putting the abstraction in its own file. The `myfirst.c` source has no wrap-around arithmetic; the wrap-around lives in `cbuf.c` where it can be tested in isolation.

### What the User Sees

After the splice, a user-space program cannot tell what shape the buffer is. `cat /dev/myfirst` still prints whatever has been written, in order. `echo hello > /dev/myfirst` still stores `hello` for later reading. The byte counters in `sysctl dev.myfirst.0.stats` still tick up by one for each byte. The new `cb_used` and `cb_free` sysctls expose the buffer's state, but the data path is byte-for-byte identical to Stage 3 of Chapter 9.

What differs is what happens *under load*. With the linear FIFO, a sustained writer eventually saw `ENOSPC` even when a reader was actively consuming bytes, because `bufhead` only collapsed back to zero when `bufused` reached zero. With the circular buffer, the writer keeps going indefinitely as long as the reader keeps up, because the free space and live bytes can occupy any combination of positions inside the underlying memory. The buffer's full capacity is now actually usable.

You will be able to see this difference clearly in Section 6 when we run `dd` against the new driver and compare the throughput numbers to Stage 3 of Chapter 9. For now, take it on faith and finish the splice.

### Handling Detach with Live Data

There is one detach-time subtlety worth addressing. With the circular buffer integrated, the buffer may hold data when the user runs `kldunload myfirst`. The Chapter 9 detach refused to unload while any descriptor was open; that check still applies. It does not, however, refuse to unload if the buffer is non-empty but no descriptor is open. Should it?

The conventional answer is no. A buffer is a transient resource. If no one is currently reading the device, the bytes in the buffer are not going to be read; the user has implicitly accepted their loss by closing all descriptors. The detach path simply frees the buffer along with everything else. If you wanted to preserve the bytes across an unload (to a file, for instance), that would be a feature, not a bug fix, and it would belong in user space, not in the driver.

We therefore make no change to the detach lifecycle. `cbuf_destroy` is called unconditionally; the bytes are released along with the backing memory.

### Wrapping Up Section 3

The driver now uses a real circular buffer. The wrap-around logic lives in a small abstraction that has its own header, its own source file, its own memory tag, and its own userland test program. The I/O handlers in `myfirst.c` are simpler than they were in Stage 3 of Chapter 9, and the tricky arithmetic is no longer scattered across them.

What you have at this point still does not handle partial reads and writes elegantly. If a user calls `read(fd, buf, 4096)` and the buffer holds 100 bytes, the loop will execute exactly once, transfer 100 bytes, and return zero with `uio_resid` reflecting the unconsumed portion. That is the correct behaviour, but the *prose* around what the user should expect, what `read(2)` returns, and how a well-written caller loops is what Section 4 is about. We will also resolve the question of what `d_read` should do when the buffer is empty and the caller is willing to wait, which is the door into non-blocking I/O in Section 5.

## Section 4: Improving the Driver's Behaviour with Partial Reads and Writes

The Stage 2 driver from the previous section already implements partial reads and writes correctly, almost by accident. The loops in `myfirst_read` and `myfirst_write` exit when the circular buffer can no longer satisfy the next iteration, leaving `uio->uio_resid` at whatever portion of the request remains unconsumed. The kernel computes the user-visible byte count as the original request size minus that residual. Both `read(2)` and `write(2)` then return that number to user space.

What we have not done is *think clearly* about what those partial transfers mean from both sides of the trust boundary. This section does that thinking. By the end of it, you will know which user-space programs handle short reads and writes correctly, which ones do not, what your driver should report when nothing at all is available, and what the rare zero-byte transfer means.

### What "Partial" Means in UNIX

A `read(2)` returns one of three things:

- A *positive integer* less than or equal to the requested count: that many bytes were placed in the caller's buffer.
- *Zero*: end of file. No more bytes will ever be produced on this descriptor; the caller should close.
- `-1`: an error occurred; the caller examines `errno` to decide what to do.

The first case is where partial transfers live. A "full" read returns exactly the requested count. A "partial" read returns fewer bytes. UNIX has always allowed partial reads, and any program that calls `read(2)` and assumes it got the full requested count is wrong. Robust programs always look at the return value and either loop until they have what they need, or accept the partial result and move on.

`write(2)` follows the same shape:

- A *positive integer* less than or equal to the requested count: that many bytes were accepted by the kernel.
- Sometimes *zero* (rarely seen in practice; usually treated as a short write of zero bytes).
- `-1`: an error occurred.

A short write means "I took this many of your bytes; please call me again with the remaining tail." Robust producers always loop until they have offered the entire payload.

### Why Drivers Should Embrace Partial Transfers

It would be tempting to make a driver always satisfy the entire request, even if it has to internally loop or wait. Some drivers do this in special cases (consider the `null` driver's read, which loops internally to deliver `ZERO_REGION_SIZE`-byte chunks until the caller's request is exhausted). For most drivers, however, embracing partial transfers is the right design choice for several reasons.

The first reason is **responsiveness**. A reader that asks for 4096 bytes and gets back 100 bytes has 100 bytes of work it can begin doing right now, instead of waiting for another 3996 bytes that may never arrive. The kernel does not have to guess how long the caller is willing to wait.

The second reason is **fairness**. If `myfirst_read` loops internally until it satisfies the entire request, a single greedy reader can hold the buffer's mutex for an indefinite time, starving every other thread that wants to access the driver. A handler that returns as soon as it cannot make progress lets the kernel scheduler preserve fairness across competing threads.

The third reason is **correctness in the face of signals**. A reader that has been waiting may receive a signal (e.g. `SIGINT` from the user pressing Ctrl-C). The kernel needs the chance to deliver that signal, which usually means returning from the current syscall. A handler that loops indefinitely never gives the kernel that chance, and the user's `kill -INT` is delayed or lost.

The fourth reason is **composition with `select(2)` / `poll(2)`**. Programs that use these readiness primitives are explicitly assuming partial-transfer semantics. They expect to be told "data is ready" and then to loop on `read(2)` until the descriptor returns zero or `EAGAIN`. A driver that always returns the full requested count breaks the polling model.

For all of these reasons, the `myfirst` driver's loops in Section 3 are designed to make a single pass through the buffer's available data, transfer what they can, and return. The next time the caller wants more, it calls `read(2)` again. This is the conventional UNIX shape.

### Reporting Accurate Byte Counts

The mechanism by which the driver reports a partial transfer is `uio->uio_resid`. The kernel sets it to the requested count before calling `d_read` or `d_write`. The handler is responsible for decrementing it as it transfers bytes. `uiomove(9)` decrements it automatically. When the handler returns, the kernel calculates the byte count as `original_resid - uio->uio_resid` and returns that to user space.

This means the handler must do exactly two things consistently:

1. Use `uiomove(9)` (or one of its companions, `uiomove_frombuf(9)`, `uiomove_nofault(9)`) to do every byte movement that crosses the trust boundary. This is what keeps `uio_resid` honest.
2. Return zero when it has done as much as it can, regardless of whether `uio_resid` is now zero or some positive number.

A handler that returns a *positive* byte count is wrong. The kernel ignores positive returns; the byte count is computed from `uio_resid`. Returning a positive integer would be silently wasteful. A handler that returns a *negative* number, or any value that is not in `errno.h`, is undefined behaviour.

A common and dangerous variant of this mistake is returning `EAGAIN` when the buffer is empty *and* having transferred some bytes earlier in the same call. The user-space `read(2)` would see `-1`/`EAGAIN`, and the bytes that were in the user buffer would silently be considered uninvolved. The right pattern is: if the handler has transferred any bytes at all, it returns 0 and lets the partial count speak for itself; only if it has transferred *zero* bytes can it return `EAGAIN`. Section 5 will codify this rule when we add non-blocking support.

### End-of-Data: When Should `d_read` Return Zero?

UNIX's "zero means EOF" rule has an interesting consequence for pseudo-devices. A regular file has a definite end: when `read(2)` reaches it, the kernel returns zero. A character device usually does not have a definite end. A serial line, a keyboard, a network device, a tape rewinding past the end of the medium: each of these *might* return zero in special cases, but in normal operation, "no data available right now" is not the same as "no data ever again."

Yet a naive `myfirst_read` that returns zero whenever the buffer is empty looks indistinguishable, from the caller's point of view, from a regular file at end-of-file. A `cat /dev/myfirst` will see zero bytes, treat it as EOF, and exit. That is not what we want. We want the reader to wait until more bytes arrive, or to be told "there are no bytes right now, but try again later" depending on the file descriptor's mode.

Two strategies are common.

The first strategy is **block by default**. `myfirst_read` waits on a sleep queue when the buffer is empty, and the writer wakes the queue when it adds bytes. Reading returns zero only if some condition signals true end-of-file (the device has been removed, the writer has explicitly closed). This is what most pseudo-devices and most TTY-style devices do. It matches `cat`'s expectation that a terminal will deliver lines as the user types them.

The second strategy is **immediate return with `EAGAIN` for non-blocking callers**. If the descriptor was opened with `O_NONBLOCK` (or the user later set the flag with `fcntl(2)`), `myfirst_read` returns `-1`/`EAGAIN` instead of blocking. This lets event-loop programs use `select(2)`, `poll(2)`, or `kqueue(2)` to multiplex many descriptors without committing to wait on any single one.

Section 5 will implement both strategies. The blocking path is the default; the non-blocking path activates when `IO_NDELAY` is set in `ioflag`. For now, in Stage 2, the driver still returns zero on empty, the same as Chapter 9 did. That is a temporary state; nothing in user space stays stable when the data path can vanish at any moment.

### Backpressure on the Write Side

The mirror of "no data right now" is "no room right now." When the buffer is full and a writer asks to add more bytes, the driver has to choose what to say.

The Stage 3 driver of Chapter 9 returned `ENOSPC`, which is the conventional signal for "the device has run out of space, permanently." That was a defensible choice in Chapter 9 because the linear FIFO genuinely could not accept more data until the buffer fully drained. With the circular buffer, however, "full" is a transient state: the writer just needs to wait until a reader has consumed something. The right return is therefore *not* `ENOSPC` in the steady state; it is either a blocking sleep until space appears, or `EAGAIN` for non-blocking callers.

The Stage 2 implementation already handles the partial-write case correctly: when the buffer fills mid-transfer, the loop exits and the user sees a write count smaller than the request. What it does *not* do yet is the right thing when the buffer is full *at the start* of the call: it returns 0 with no bytes transferred, which the kernel turns into a `write(2)` return of zero. A return of zero from `write(2)` is technically legal but is an odd thing to see, and most user programs will treat it as an error or loop forever waiting for it to become non-zero.

The conventional fix, again, is mode-dependent. A blocking writer should sleep until space is available; a non-blocking writer should receive `EAGAIN`. We will implement both in Section 5. The structure of the Stage 2 loop is already correct for both cases; what is missing is the choice about what to do when *no* progress was made on the first iteration.

### Zero-Length Reads and Writes

A zero-length read or write is a perfectly legal call. `read(fd, buf, 0)` and `write(fd, buf, 0)` are valid syscalls; they exist explicitly so that programs can validate a file descriptor without committing to a transfer. The kernel passes them down to the driver with `uio->uio_resid == 0`.

Your handler must not panic, error, or loop in this case. The Stage 2 driver naturally does the right thing: the `while (uio->uio_resid > 0)` loop never executes, and the handler returns 0 with `uio_resid` still 0. The user sees `read(2)` or `write(2)` return zero. Programs that call zero-length I/O for descriptor validation get the result they expect.

Be cautious about adding "is the request empty?" early returns at the start of your handler. They look like a small optimization but they introduce branching that is easy to get wrong. The Chapter 9 cheat sheet's rule applies: `if (uio->uio_resid == 0) return (EINVAL);` is a bug.

### A User-Space Walk Through the Loop

Watching what a user program does with a partial transfer is the best way to internalise the contract. Here is a small reader written in idiomatic UNIX style:

```c
static int
read_all(int fd, void *buf, size_t want)
{
        char *p = buf;
        size_t left = want;
        ssize_t n;

        while (left > 0) {
                n = read(fd, p, left);
                if (n < 0) {
                        if (errno == EINTR)
                                continue;
                        return (-1);
                }
                if (n == 0)
                        break;          /* EOF */
                p += n;
                left -= n;
        }
        return (int)(want - left);
}
```

`read_all` keeps calling `read(2)` until either it has all `want` bytes, or it sees end-of-file, or it sees a real error. Short reads are absorbed transparently. A `EINTR` from a signal causes a retry. The function returns the actual number of bytes obtained.

A correctly written `write_all` is the mirror image:

```c
static int
write_all(int fd, const void *buf, size_t have)
{
        const char *p = buf;
        size_t left = have;
        ssize_t n;

        while (left > 0) {
                n = write(fd, p, left);
                if (n < 0) {
                        if (errno == EINTR)
                                continue;
                        return (-1);
                }
                if (n == 0)
                        break;          /* unexpected; treat as error */
                p += n;
                left -= n;
        }
        return (int)(have - left);
}
```

`write_all` calls `write(2)` repeatedly until the whole payload has been accepted by the kernel. Short writes are absorbed transparently. A `EINTR` causes a retry. The function returns the number of bytes accepted.

Both helpers belong in the same file (or a shared utility header) because they are nearly always used together. They are short, robust, and they make user-space code that talks to your driver behave correctly even when the driver is doing partial transfers. We will use both in the test programs you build in Section 6.

### What `cat`, `dd`, and Friends Actually Do

The base-system tools you have been using to test the driver each handle short reads and writes differently. It is worth knowing what each does, so that you can interpret what you see.

`cat(1)` reads with a buffer of `MAXBSIZE` (16 KB on FreeBSD 14.3) and writes whatever it gets in a loop. Short reads from the source descriptor are absorbed; `cat` simply makes another `read(2)` call. Short writes to the destination descriptor are also absorbed; `cat` writes the unconsumed tail in a follow-up call. As far as `cat` is concerned, the size of the transfers does not matter; it just keeps moving bytes until it sees end-of-file on the source.

`dd(1)` is more rigid. It reads in blocks of `bs=` bytes (default 512) and writes whatever it received in the same block size. Crucially, `dd` *does not* loop on a short read by default. If `read(2)` returns 100 bytes when `bs=4096`, `dd` writes a 100-byte block and increments its short-read counter. The output you see at the end (`X+Y records in / X+Y records out`) is split between full records (`X`) and short records (`Y`). The total byte count is what matters; the split tells you whether the source was producing short reads.

There is a `dd` flag, `iflag=fullblock`, that makes it loop on the source the way `cat` does. Use it when you want to test throughput without short-read noise: `dd if=/dev/myfirst of=/dev/null bs=4k iflag=fullblock`. Without that flag, you will see records split by every short read.

`hexdump(1)` reads one byte at a time by default but can be told to read larger blocks. It does not care about short reads from the source.

`truss(1)` traces every syscall, including the byte counts returned by each. Running a producer or consumer under `truss` is the most direct way to see what byte counts your driver is returning. If you run `truss -f -t read,write cat /dev/myfirst`, the output will tell you exactly how many bytes each `read(2)` returned, and you can correlate that with `cb_used` in `sysctl`.

### Common Mistakes in Partial-Transfer Code

The following mistakes are the ones that show up most often in beginner driver code. Each one has the same shape: the handler does something that looks reasonable in a single test case and silently misbehaves under load.

**Mistake 1: returning the byte count from `d_read` or `d_write`.** A handler that does `return ((int)nbytes);` instead of `return (0);` is wrong. The kernel ignores the positive value (because positive returns are not valid errno values) and computes the byte count from `uio_resid`. The handler that returns `nbytes` and *also* does the right thing with `uiomove` accidentally works; the handler that returns `nbytes` and skips the `uiomove` step silently corrupts data. Do not invent your own return convention.

**Mistake 2: returning `EAGAIN` after a partial transfer.** A handler that has already consumed some bytes from `uio` and then returns `EAGAIN` because no more are available silently throws away the bytes the user already got. The correct rule is: if you transferred any bytes, return 0; only if you transferred zero bytes can you return an errno like `EAGAIN`.

**Mistake 3: refusing zero-length transfers.** As noted above, `read(fd, buf, 0)` and `write(fd, buf, 0)` are legal. A handler that returns `EINVAL` on zero `uio_resid` breaks programs that use zero-length I/O for descriptor validation.

**Mistake 4: looping inside the handler when the buffer is empty.** A handler that spins inside the kernel waiting for data to appear blocks the calling thread *and* every thread that wants to acquire the same lock. The right mechanism for waiting is `mtx_sleep(9)` or `cv_wait(9)`, not a busy loop. Section 5 covers this.

**Mistake 5: holding the buffer mutex across `uiomove`.** This is the single most common bug in beginner driver code. `uiomove` may sleep on a page fault. Sleeping while holding a non-sleepable mutex is a `KASSERT` panic on `INVARIANTS`-enabled kernels and a `WITNESS` warning on kernels with `WITNESS` enabled; on a production kernel built without either, the same pattern can still deadlock the machine or silently corrupt state when the page fault tries to page in a user page. Either way, the behaviour is wrong, and the testing kernel should catch it before production ever does. The Stage 2 handlers carefully release the mutex before calling `uiomove`. Repeat the pattern in every new handler you write.

**Mistake 6: not honouring the user's signal.** A blocking handler that does not pass `PCATCH` to `mtx_sleep(9)` or `tsleep(9)` cannot be interrupted by a signal. The user's Ctrl-C is silently ignored, and only `kill -9` will free the thread. Always allow signals to interrupt a wait, and always handle the resulting `EINTR` cleanly.

**Mistake 7: trusting `uio->uio_resid` after a failure.** When `uiomove` returns a non-zero error (for instance, `EFAULT` because the user-space buffer is invalid), `uio_resid` may be partially decremented or fully decremented depending on where in the transfer the fault occurred. The convention is: propagate the error, do not retry, and accept that the byte count seen by the user may include some bytes that arrived before the fault. This is rare in practice, and the user gets `EFAULT` plus a byte count that lets them recover.

### A Concrete Example: Watching Partial Reads

To make this real, load the Stage 2 driver, write a few hundred bytes into it, and watch a small reader collect them in chunks. With the driver loaded:

```sh
$ printf 'aaaaaaaaaaaaaaaaaaaa' > /dev/myfirst              # 20 bytes
$ printf 'bbbbbbbbbbbbbbbbbbbb' > /dev/myfirst              # 20 more
$ sysctl dev.myfirst.0.stats.cb_used
dev.myfirst.0.stats.cb_used: 40
```

The buffer holds 40 bytes. Now run a small reader, traced by `truss`:

```c
/* shortreader.c */
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

int
main(void)
{
        int fd = open("/dev/myfirst", O_RDONLY);
        char buf[1024];
        ssize_t n;

        n = read(fd, buf, sizeof(buf));
        printf("read 1: %zd\n", n);
        n = read(fd, buf, sizeof(buf));
        printf("read 2: %zd\n", n);
        close(fd);
        return (0);
}
```

```sh
$ cc -o shortreader shortreader.c
$ truss -t read,write ./shortreader
... read(3, ...) = 40 (0x28)
read 1: 40
... read(3, ...) = 0 (0x0)
read 2: 0
```

The first `read(2)` returned 40 even though the user asked for 1024. That is a partial read, and it is correct. The second `read(2)` returned 0 because the buffer was empty. With Stage 2 the zero is a stand-in for "no data right now"; with Stage 3 (after we add blocking) the second read will sleep until more data arrives.

Now do the same with a tighter buffer to see partial reads on a larger transfer:

```sh
$ dd if=/dev/zero bs=1m count=8 | dd of=/dev/myfirst bs=4096 2>/tmp/dd-w &
$ dd if=/dev/myfirst of=/dev/null bs=512 2>/tmp/dd-r
```

When the writer is producing 4096-byte blocks faster than the reader is consuming 512-byte blocks, the buffer fills up. The writer's `write(2)` calls start returning short counts, and `dd` records each short call as a partial record. The reader keeps reading 512 at a time. When you stop both processes, look at the `records out` line in `/tmp/dd-w` and the `records in` line in `/tmp/dd-r`; the second number on each line is the count of short records.

This is healthy behaviour. The driver is doing exactly what a UNIX device should do: let each side proceed at its own pace, report partial transfers honestly, and never block when there is nothing to wait for. Without partial-transfer semantics, the writer would have hit `ENOSPC` (the Chapter 9 behaviour) and `dd` would have stopped.

### Wrapping Up Section 4

The driver's read and write handlers are now correctly partial-transfer-aware. We have not changed the code from Section 3; we have only made the behaviour explicit and built up the vocabulary needed to talk about it. You know what `read(2)` and `write(2)` return when only some of the bytes are available, you know how to write user-space loops that handle those returns, and you know which base-system tools handle partial transfers gracefully and which need a flag.

What is still missing is the right behaviour when the buffer is *completely* empty (for reads) or *completely* full (for writes). The Stage 2 driver still returns zero or stops with no progress; that is a stand-in for the more correct behaviour we are about to add. Section 5 introduces non-blocking I/O, the blocking sleep path, and `EAGAIN`. After that, the driver will behave correctly under all combinations of fill state and caller mode.

## Section 5: Implementing Non-Blocking I/O

Up to this point in the chapter, the driver has been doing one of two things when a caller asks for a transfer that cannot be satisfied right now. It returns zero (on read, mimicking end-of-file) or it stops mid-loop with no bytes transferred (on write, telling the user "zero bytes accepted"). Neither behaviour is what a real character device should do. This section replaces both with the two correct behaviours: a blocking wait for the default case, and a clean `EAGAIN` for callers that opened the descriptor non-blocking.

Before we touch the driver, let us make sure we understand what "non-blocking" means from each side of the trust boundary. That vocabulary is what ties the implementation together.

### What Non-Blocking I/O Is

A **blocking** descriptor is one for which `read(2)` and `write(2)` are allowed to sleep. If the driver has no data available, `read(2)` waits; if the driver has no room available, `write(2)` waits. The calling thread is suspended, possibly for a long time, until progress can be made. This is the default behaviour of every file descriptor in UNIX.

A **non-blocking** descriptor is one for which `read(2)` and `write(2)` must *never* sleep. If the driver has no data right now, `read(2)` returns `-1` with `errno = EAGAIN`. If the driver has no room right now, `write(2)` returns `-1` with `errno = EAGAIN`. The caller is expected to do something else (typically call `select(2)`, `poll(2)`, or `kqueue(2)` to find out when the descriptor becomes ready) and then try again.

The per-descriptor flag that turns non-blocking mode on or off is `O_NONBLOCK`. A program sets it either at `open(2)` time (`open(path, O_RDONLY | O_NONBLOCK)`) or later with `fcntl(2)` (`fcntl(fd, F_SETFL, O_NONBLOCK)`). The flag lives in the descriptor's `f_flag` field, which is private to the file structure; the driver does not see the flag directly.

What the driver *does* see is the `ioflag` argument to `d_read` and `d_write`. The devfs layer translates the descriptor's flags into bits of the `ioflag` that the handler can check. Specifically:

- `IO_NDELAY` is set when the descriptor has `O_NONBLOCK`.
- `IO_DIRECT` is set when the descriptor has `O_DIRECT`.
- `IO_SYNC` is set on `d_write` when the descriptor has `O_FSYNC`.

The translation is even simpler than it looks. A `CTASSERT` in `/usr/src/sys/fs/devfs/devfs_vnops.c` declares that `O_NONBLOCK == IO_NDELAY`. The bit values are chosen so the two names are interchangeable, and you can write `(ioflag & IO_NDELAY)` or `(ioflag & O_NONBLOCK)` depending on which convention feels clearer. Both work. The FreeBSD source tree uses `IO_NDELAY` more often, so we will follow that.

### When Non-Blocking Behaviour Is Useful

Non-blocking mode is the underlying mechanism that makes event-driven programs possible. Without it, a single thread that wants to read from several descriptors has to pick one, block on it, and ignore the others until it wakes up. With it, a single thread can test several descriptors for readiness, process whichever is ready, and loop back without ever committing to sleep on any single one.

Three common programs rely on this mode heavily. A classical event loop (`libevent`, `libev`, or the now-standard `kqueue`-based pattern in FreeBSD) does nothing but wait in `kevent(2)` for an event, dispatch it, and loop. A networking daemon (`nginx`, `haproxy`) uses the same shape to juggle thousands of connections per thread. A real-time application (audio processing, industrial control) needs bounded worst-case latency and cannot afford a long block.

A driver that wants to play nicely with these programs must implement non-blocking mode correctly. Returning the wrong errno, sleeping when `IO_NDELAY` is set, or forgetting to notify `poll(2)` when state changes each produce bugs that are hard to diagnose.

### The `IO_NDELAY` Flag: How It Flows to the Driver

Trace the flow once so you know where the flag comes from. The user calls `read(fd, buf, n)` on a descriptor that has `O_NONBLOCK` set. Inside the kernel:

1. `sys_read` looks up the file descriptor and finds a `struct file` with `fp->f_flag` containing `O_NONBLOCK`.
2. `vn_read` or (for character devices) `devfs_read_f` assembles an `ioflag` by masking `fp->f_flag` for the bits that drivers care about. In particular, it computes `ioflag = fp->f_flag & (O_NONBLOCK | O_DIRECT);`.
3. The computed `ioflag` is passed to the driver's `d_read`.

From the driver's point of view, the translation is complete: `ioflag & IO_NDELAY` is true if and only if the caller wants non-blocking semantics. A missing bit means block-if-needed. An extra bit means non-block-and-return-EAGAIN-if-needed.

On the write side the same pattern applies. `devfs_write_f` computes `ioflag = fp->f_flag & (O_NONBLOCK | O_DIRECT | O_FSYNC);` and passes it in. The write handler's check is symmetric: `ioflag & IO_NDELAY` is "do not block."

### The `EAGAIN` Convention

When the driver's handler decides that it cannot make progress and the caller is non-blocking, it returns `EAGAIN`. The kernel's generic layer passes this through as `-1` / `errno = EAGAIN` at the user level. The user is expected to treat `EAGAIN` as "this descriptor is not ready; wait or try later," not as an error in the traditional sense.

Two details about `EAGAIN` are worth committing to memory.

First, `EAGAIN` and `EWOULDBLOCK` are the same value in FreeBSD. They are two names for a single errno. Some older man pages use `EWOULDBLOCK` for socket-related contexts and `EAGAIN` for file-related contexts; the compatibility is tight, and either name is acceptable in driver code. The FreeBSD source tree uses `EAGAIN` almost exclusively for drivers.

Second, `EAGAIN` must only be returned when the handler has transferred *zero* bytes. If the handler has already moved some bytes via `uiomove` and then wants to stop because no more can move right now, it must return 0 (not `EAGAIN`). The kernel will compute the partial byte count from `uio_resid` and deliver that to the user. A subsequent call from the user will then see `EAGAIN` because the buffer is still empty. The rule is: `EAGAIN` means "no progress at all on this call"; a partial transfer means "progress, but less than requested, and now you need to retry for the rest."

This is exactly the rule Section 4 introduced. Here we operationalise it in code.

### The Blocking Path: `mtx_sleep(9)` and `wakeup(9)`

The blocking path is the default behaviour for a descriptor without `O_NONBLOCK`. When the buffer is empty, the reader sleeps; when a writer adds bytes, it wakes the reader. FreeBSD provides this with a pair of primitives that compose with mutexes.

`mtx_sleep(void *chan, struct mtx *mtx, int priority, const char *wmesg, sbintime_t timo)` puts the calling thread to sleep on the "channel" `chan` (an arbitrary address used as a key), atomically releasing `mtx`. When the thread wakes, it reacquires `mtx` before returning. The `priority` argument may include `PCATCH` to allow signal delivery to interrupt the sleep, and `wmesg` is a short human-readable name that shows up in `ps -AxH` and similar tools. The `timo` argument specifies a maximum sleep time; zero means no timeout.

`wakeup(void *chan)` wakes *every* thread sleeping on `chan`. `wakeup_one(void *chan)` wakes only one. For a single-reader driver, `wakeup` is fine; for a multi-reader driver where we want to hand off one chunk of work to one reader, `wakeup_one` is often right. For `myfirst` we will use `wakeup` because we may have both a producer and a consumer waiting, and we want to make sure neither is starved.

The contract between the two is that the sleeper must hold the mutex, check the condition, and call `mtx_sleep` *without* releasing the mutex in between. `mtx_sleep` atomically drops the lock and sleeps; when it returns, the lock is reacquired and the sleeper must re-check the condition (spurious wake-ups are possible; a concurrent thread may have taken the byte we were waiting for). The pattern is the classic `while (condition) mtx_sleep(...)` loop.

A minimal blocking read in our driver looks like this:

```c
mtx_lock(&sc->mtx);
while (cbuf_used(&sc->cb) == 0) {
        if (ioflag & IO_NDELAY) {
                mtx_unlock(&sc->mtx);
                return (EAGAIN);
        }
        error = mtx_sleep(&sc->cb, &sc->mtx, PCATCH,
            "myfrd", 0);
        if (error != 0) {
                mtx_unlock(&sc->mtx);
                return (error);
        }
        if (!sc->is_attached) {
                mtx_unlock(&sc->mtx);
                return (ENXIO);
        }
}
/* ... now proceed to read from the cbuf ... */
```

Four points deserve comment.

The first is the **condition in the while loop**. We check `cbuf_used(&sc->cb) == 0`. As long as that is true, we sleep. The while-check is essential: `mtx_sleep` can return for reasons other than "data appeared" (signals, timeouts, spurious wake-ups, or a different thread having consumed the data before we could). After every return from `mtx_sleep`, we must re-check.

The second is the **EAGAIN path**. If the caller is non-blocking and the buffer is empty, we release the lock and return `EAGAIN` without sleeping. The check has to happen *before* `mtx_sleep`, not after; otherwise we would sleep, wake up, then discover the caller was non-blocking all along.

The third is the **PCATCH**. With `PCATCH`, `mtx_sleep` can return `EINTR` or `ERESTART` if a signal is delivered. Propagating that return to the user is the whole purpose of `PCATCH`: we want the user's Ctrl-C to actually interrupt the read. Without `PCATCH`, `SIGINT` is held until the sleep completes for some other reason, and the user gets a long, unexplained hang.

The fourth is the **detach check**. After `mtx_sleep` returns, it is possible that `myfirst_detach` has begun and that `sc->is_attached` is now zero. We check and return `ENXIO` if so. This prevents a read from proceeding against a partially torn-down driver. The detach code path has to call `wakeup(&sc->cb)` to release any sleepers before tearing the mutex down; we will add that call below.

### The Writer Side

The write path is the mirror image:

```c
mtx_lock(&sc->mtx);
while (cbuf_free(&sc->cb) == 0) {
        if (ioflag & IO_NDELAY) {
                mtx_unlock(&sc->mtx);
                return (EAGAIN);
        }
        error = mtx_sleep(&sc->cb, &sc->mtx, PCATCH,
            "myfwr", 0);
        if (error != 0) {
                mtx_unlock(&sc->mtx);
                return (error);
        }
        if (!sc->is_attached) {
                mtx_unlock(&sc->mtx);
                return (ENXIO);
        }
}
/* ... now proceed to write into the cbuf ... */
```

The same four points apply: check the condition in a `while` loop, handle `IO_NDELAY` before sleeping, pass `PCATCH`, re-check `is_attached` after the sleep. Notice that both sleepers use the same "channel" (`&sc->cb`). That is deliberate. When a reader transfers bytes out of the buffer, it calls `wakeup(&sc->cb)` to unblock any writer waiting for space. When a writer transfers bytes into the buffer, it calls `wakeup(&sc->cb)` to unblock any reader waiting for data. A single channel that wakes "everything on this buffer" is simple and correct.

Some drivers use two separate channels (one for readers, one for writers) so that a reader's `wakeup` only disturbs writers and vice versa. That is a valid optimisation when you have many readers or many writers. For a pseudo-device whose expected use is one producer and one consumer, a single channel is both simpler and sufficient.

### The Full Stage 3 Handlers

Putting the non-blocking checks into the Stage 2 handlers gives us Stage 3. Here is the full shape:

```c
static int
myfirst_read(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct myfirst_softc *sc = dev->si_drv1;
        struct myfirst_fh *fh;
        char bounce[MYFIRST_BOUNCE];
        size_t take, got;
        ssize_t nbefore;
        int error;

        error = devfs_get_cdevpriv((void **)&fh);
        if (error != 0)
                return (error);
        if (sc == NULL || !sc->is_attached)
                return (ENXIO);

        nbefore = uio->uio_resid;

        while (uio->uio_resid > 0) {
                mtx_lock(&sc->mtx);
                while (cbuf_used(&sc->cb) == 0) {
                        if (uio->uio_resid != nbefore) {
                                /*
                                 * We already transferred some bytes
                                 * in an earlier iteration; report
                                 * success now rather than block further.
                                 */
                                mtx_unlock(&sc->mtx);
                                return (0);
                        }
                        if (ioflag & IO_NDELAY) {
                                mtx_unlock(&sc->mtx);
                                return (EAGAIN);
                        }
                        error = mtx_sleep(&sc->cb, &sc->mtx, PCATCH,
                            "myfrd", 0);
                        if (error != 0) {
                                mtx_unlock(&sc->mtx);
                                return (error);
                        }
                        if (!sc->is_attached) {
                                mtx_unlock(&sc->mtx);
                                return (ENXIO);
                        }
                }
                take = MIN((size_t)uio->uio_resid, sizeof(bounce));
                got = cbuf_read(&sc->cb, bounce, take);
                sc->bytes_read += got;
                fh->reads += got;
                mtx_unlock(&sc->mtx);

                wakeup(&sc->cb);        /* space may have freed for writers */

                error = uiomove(bounce, got, uio);
                if (error != 0)
                        return (error);
        }
        return (0);
}

static int
myfirst_write(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct myfirst_softc *sc = dev->si_drv1;
        struct myfirst_fh *fh;
        char bounce[MYFIRST_BOUNCE];
        size_t want, put, room;
        ssize_t nbefore;
        int error;

        error = devfs_get_cdevpriv((void **)&fh);
        if (error != 0)
                return (error);
        if (sc == NULL || !sc->is_attached)
                return (ENXIO);

        nbefore = uio->uio_resid;

        while (uio->uio_resid > 0) {
                mtx_lock(&sc->mtx);
                while ((room = cbuf_free(&sc->cb)) == 0) {
                        if (uio->uio_resid != nbefore) {
                                mtx_unlock(&sc->mtx);
                                return (0);
                        }
                        if (ioflag & IO_NDELAY) {
                                mtx_unlock(&sc->mtx);
                                return (EAGAIN);
                        }
                        error = mtx_sleep(&sc->cb, &sc->mtx, PCATCH,
                            "myfwr", 0);
                        if (error != 0) {
                                mtx_unlock(&sc->mtx);
                                return (error);
                        }
                        if (!sc->is_attached) {
                                mtx_unlock(&sc->mtx);
                                return (ENXIO);
                        }
                }
                mtx_unlock(&sc->mtx);

                want = MIN((size_t)uio->uio_resid, sizeof(bounce));
                want = MIN(want, room);
                error = uiomove(bounce, want, uio);
                if (error != 0)
                        return (error);

                mtx_lock(&sc->mtx);
                put = cbuf_write(&sc->cb, bounce, want);
                sc->bytes_written += put;
                fh->writes += put;
                mtx_unlock(&sc->mtx);

                wakeup(&sc->cb);        /* data may have appeared for readers */
        }
        return (0);
}
```

Three patterns in this code are worth studying carefully.

The first is the **"transferred any bytes?"** test at the top of the inner loop. `uio->uio_resid != nbefore` is true if any previous iteration transferred data. When that condition holds and the buffer is now empty (read) or full (write), we return 0 immediately instead of blocking. The kernel will report the partial transfer to user space, and the next call will decide whether to block or to return `EAGAIN`. This is the rule from Section 4 in code form: a handler that has already made progress must return 0, not `EAGAIN`, and not a deeper block.

The second is the **`wakeup` after progress**. When the reader drains bytes, space has freed; the writer may be waiting for space, and we wake it. When the writer adds bytes, data has appeared; the reader may be waiting for data, and we wake it. Every state change is paired with a `wakeup`. Missing a `wakeup` causes threads to sleep forever (or until a timer fires, if one exists); spurious `wakeup` calls are harmless because the while-loops re-check the condition.

The third is the **order of `mtx_unlock` and `uiomove`**. The handler holds the lock while it manipulates the cbuf, then releases the lock *before* calling `uiomove`. `uiomove` may sleep; sleeping under a mutex is a bug. Notice also that on the write side, the handler takes a snapshot of `room` while holding the lock, uses that snapshot to size the bounce, and releases the lock before `uiomove`. If a concurrent thread has modified the buffer while the handler was copying out of user space, the subsequent `cbuf_write` may store less than `want` bytes (the clamp in `cbuf_write` ensures it is safe). In our current single-writer design this race is never triggered, but the code handles it for free.

### Waking Sleepers on Detach

We also need to teach `myfirst_detach` to release any sleepers. The pattern is:

```c
static int
myfirst_detach(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);

        mtx_lock(&sc->mtx);
        if (sc->active_fhs > 0) {
                mtx_unlock(&sc->mtx);
                device_printf(dev,
                    "Cannot detach: %d open descriptor(s)\n",
                    sc->active_fhs);
                return (EBUSY);
        }
        sc->is_attached = 0;
        wakeup(&sc->cb);                /* release any sleepers */
        mtx_unlock(&sc->mtx);

        /* ... destroy_dev, cbuf_destroy, mtx_destroy, sysctl_ctx_free ... */
        return (0);
}
```

Two details about this code are specific to Chapter 10.

The first is that we set `is_attached = 0` *before* calling `wakeup`. A sleeper that wakes now will see the flag and return `ENXIO` in the blocking loop; a sleeper that has not yet slept will see the flag and return `ENXIO` without ever sleeping. Setting the flag after `wakeup` would allow a race where a sleeper re-acquires the lock, finds the condition still true (buffer empty), and goes back to sleep, *while* the detach is waiting to tear down the mutex.

The second is that the detach checks `active_fhs > 0` and refuses to proceed if any descriptor is open. This is the same check from Chapter 9. It means a sleeper is always holding a descriptor open, which means the detach will not be running concurrently with a sleeper. The `wakeup` call is there as a belt-and-braces check: if a future refactor ever allows detach while a descriptor is still open, the sleepers will not be stuck.

### Adding `d_poll` for `select(2)` and `poll(2)`

A non-blocking caller that receives `EAGAIN` needs some way to be notified when the descriptor becomes ready. `select(2)` and `poll(2)` are the classic mechanisms for that; `kqueue(2)` is the modern one. We will implement the classic two here and leave `kqueue` for Chapter 11 (where `d_kqfilter` and `knlist` infrastructure belongs).

The `d_poll` handler is simple in shape:

```c
static int
myfirst_poll(struct cdev *dev, int events, struct thread *td)
{
        struct myfirst_softc *sc = dev->si_drv1;
        int revents = 0;

        mtx_lock(&sc->mtx);
        if (events & (POLLIN | POLLRDNORM)) {
                if (cbuf_used(&sc->cb) > 0)
                        revents |= events & (POLLIN | POLLRDNORM);
                else
                        selrecord(td, &sc->rsel);
        }
        if (events & (POLLOUT | POLLWRNORM)) {
                if (cbuf_free(&sc->cb) > 0)
                        revents |= events & (POLLOUT | POLLWRNORM);
                else
                        selrecord(td, &sc->wsel);
        }
        mtx_unlock(&sc->mtx);
        return (revents);
}
```

`d_poll` receives the events the user is interested in and must return the subset that are currently ready. For `POLLIN`/`POLLRDNORM` (readable), we return ready if the buffer has any bytes. For `POLLOUT`/`POLLWRNORM` (writable), we return ready if the buffer has any free space. If neither is ready, we call `selrecord(td, &sc->rsel)` or `selrecord(td, &sc->wsel)` to register the calling thread so that we can wake it later.

Two new fields are needed in the softc: `struct selinfo rsel;` and `struct selinfo wsel;`. `selinfo` is the kernel's per-condition record of pending `select(2)`/`poll(2)` waiters. It is declared in `/usr/src/sys/sys/selinfo.h`.

The read and write handlers need a matching `selwakeup(9)` call whenever the buffer transitions from empty to non-empty or from full to non-full. `selwakeup(9)` is the plain form; FreeBSD 14.3 also exposes `selwakeuppri(9)`, which wakes the registered threads at a specified priority and is commonly used by network and storage code that wants latency-sensitive wakeups. For a general-purpose pseudo-device, plain `selwakeup` is the right default. We add the calls next to the `wakeup(&sc->cb)` calls:

```c
/* In myfirst_read, after a successful cbuf_read: */
mtx_unlock(&sc->mtx);
wakeup(&sc->cb);
selwakeup(&sc->wsel);   /* space is now available for writers */

/* In myfirst_write, after a successful cbuf_write: */
mtx_unlock(&sc->mtx);
wakeup(&sc->cb);
selwakeup(&sc->rsel);   /* data is now available for readers */
```

Attach initialises the `selinfo` fields with `knlist_init_mtx(&sc->rsel.si_note, &sc->mtx);` and `knlist_init_mtx(&sc->wsel.si_note, &sc->mtx);` if you plan to support `kqueue(2)` later. For pure `select(2)`/`poll(2)` support, the `selinfo` structure is zero-initialised by the softc allocation and needs no further setup.

Detach must call `seldrain(&sc->rsel);` and `seldrain(&sc->wsel);` before freeing the softc, to tear down any lingering selection records.

Add `.d_poll = myfirst_poll,` to the `myfirst_cdevsw` initialiser, and the driver's `select(2)`/`poll(2)` story is complete.

### How a Non-Blocking Caller Uses All of This

Putting the pieces together, here is what a well-written non-blocking reader looks like against `myfirst`:

```c
int fd = open("/dev/myfirst", O_RDONLY | O_NONBLOCK);
char buf[1024];
ssize_t n;
struct pollfd pfd = { .fd = fd, .events = POLLIN };

for (;;) {
        n = read(fd, buf, sizeof(buf));
        if (n > 0) {
                /* got some bytes; process them */
        } else if (n == 0) {
                /* EOF; our driver never reaches this case yet */
                break;
        } else if (errno == EAGAIN) {
                /* no data; wait for readiness */
                poll(&pfd, 1, -1);
        } else if (errno == EINTR) {
                /* signal; retry */
        } else {
                perror("read");
                break;
        }
}
close(fd);
```

The loop reads until it gets data or `EAGAIN`. On `EAGAIN`, it calls `poll(2)` to wait until the kernel reports the descriptor readable, then loops back. The `POLLIN` event will be reported when `myfirst_write` runs the `selwakeup(&sc->rsel)` call that follows a successful `cbuf_write`. The driver's `d_poll` is the bridge between the kernel's `select/poll` machinery and the buffer's state.

This is the canonical shape of event-driven UNIX I/O, and your driver now participates in it correctly.

### A Note About `O_NONBLOCK` and `select`/`poll` Composition

It is worth understanding how `select(2)` / `poll(2)` and `O_NONBLOCK` interact. The conventional rule is that a program uses both together: it registers the descriptor with `poll` and then reads from it. Using either alone is valid but less common.

If a program uses `O_NONBLOCK` without `poll`, it will busy-spin. On every `EAGAIN` it will have to `sleep` or `usleep` before retrying, wasting cycles for no good reason. This is almost always wrong, but it works.

If a program uses `poll` without `O_NONBLOCK`, the `poll` reports readiness and then `read(2)` does a blocking call. The blocking call will complete almost immediately in the normal case, because the condition was just reported ready. However, in the rare case where the kernel's state changes between the `poll` return and the `read` call (another thread drained the buffer, for instance), the `read` will block indefinitely. This is a subtle bug, and most event-driven libraries defend against it by always combining `poll` with `O_NONBLOCK`.

The `myfirst` driver supports both patterns correctly. A well-written program combines the two; a less-careful program will work in simple cases and have the corner case described above.

### Observing the Blocking Path in Action

Load the Stage 3 driver and run a quick experiment:

```sh
$ kldload ./myfirst.ko
$ cat /dev/myfirst &
[1] 12345
```

The `cat` is now blocked inside `myfirst_read`, sleeping on `&sc->cb`. You can confirm with `ps`:

```sh
$ ps -AxH | grep cat
12345  -  S+    0:00.00 myfrd
```

The `S+` state indicates that the process is sleeping, and the `wmesg` column shows `myfrd`, which is exactly the string we passed to `mtx_sleep`. Now write into the driver from another terminal:

```sh
$ echo hello > /dev/myfirst
```

The `cat` wakes up, reads `hello`, and either prints it and blocks again, or (if the device is closed by the writer) reaches end-of-file and exits. In our current Stage 3 there is no "writer has closed" mechanism, so the `cat` blocks again after printing. Use Ctrl-C in its terminal to interrupt it:

```sh
$ kill -INT %1
```

Because we passed `PCATCH` to `mtx_sleep`, the signal wakes the sleeper, which returns `EINTR`, which propagates out to `cat` as a failed `read(2)`. `cat` sees it, notices the signal, and exits cleanly.

This is the whole blocking path in action. Nothing mysterious happened; each piece is visible in the source and in `ps`.

### Common Mistakes in the Blocking Path

Two mistakes are especially common in this material.

**Mistake 1: forgetting to release the mutex before returning `EAGAIN`.** The code above explicitly unlocks before every `return` in the sleep loop. If you forget one of those unlocks, subsequent attempts to take the mutex will panic or deadlock. A `WITNESS` kernel will catch this immediately in a lab environment.

**Mistake 2: using `tsleep(9)` when you should use `mtx_sleep(9)`.** `tsleep` does not take a mutex argument; it assumes the caller is not holding any interlock. In a driver that uses `mtx_sleep`, the mutex is dropped atomically with the sleep; with `tsleep`, you would have to drop the mutex yourself and then re-acquire it after waking, introducing a race window where a producer can add data and call `wakeup` before you are back on the sleep queue. `mtx_sleep` is the correct primitive for every case where you hold a mutex and want to sleep while releasing it.

**Mistake 3: not handling the `PCATCH` return values.** `mtx_sleep` with `PCATCH` can return `0`, `EINTR`, `ERESTART`, or `EWOULDBLOCK` (for timeouts). In driver code the conventional thing is to return `error` without inspecting it further; the kernel knows how to translate `ERESTART` into a syscall restart when the process's signal disposition allows it. Inspecting the value and returning `0` only for `error == 0` is the pattern in the Stage 3 code above.

**Mistake 4: using different "channels" for `mtx_sleep` and `wakeup`.** The sleeper uses `&sc->cb` as the channel; the waker must use exactly the same address. A common bug is for one site to use `sc` (the softc pointer) and another to use `&sc->cb`. The sleepers will never wake until a timeout fires or a different wakeup happens to match. Double-check that every `mtx_sleep` / `wakeup` pair uses the same channel.

### Wrapping Up Section 5

The driver now handles both blocking and non-blocking callers correctly. A blocking reader sleeps on an empty buffer and wakes when a writer deposits data. A non-blocking reader receives `EAGAIN` immediately on an empty buffer. The symmetric pair applies to writers. `select(2)` and `poll(2)` are supported through `d_poll` and the `selinfo` machinery, and a well-behaved event-loop program can now multiplex `/dev/myfirst` with other descriptors. Detach releases any sleepers before tearing the driver down.

What you have built is a competently behaved character device. It moves bytes efficiently, cooperates with the kernel's readiness and sleep primitives, and honours the user-facing conventions of UNIX I/O. What remains in the rest of the chapter is to test it rigorously (Section 6), refactor it for concurrency work (Section 7), and explore three supplementary topics that often appear alongside this material in real drivers (`d_mmap`, zero-copy thinking, and the throughput patterns of readahead and write coalescing).

## Section 6: Testing Buffered I/O with User Programs

A driver is only as reliable as the tests you run against it. Chapters 7 through 9 established a small test kit (a short `rw_myfirst` exerciser, plus `cat`, `echo`, `dd`, and `hexdump`). Chapter 10 pushes that kit further because the new behaviour the driver now exhibits (blocking, non-blocking, partial I/O, wrap-around) only shows up under realistic load. This section builds out three new user-space tools and walks through a consolidated test plan you can work through after each stage.

The tools in this section live under `examples/part-02/ch10-handling-io-efficiently/userland/`. They are deliberately small. The longest is under 150 lines. Each one exists to exercise a specific pattern the driver is now supposed to handle, and each one produces output that you can read and verify.

### Three Tools We Will Build

`rw_myfirst_nb.c` is a non-blocking tester. It opens the device with `O_NONBLOCK`, issues a read, expects `EAGAIN`, writes some bytes, issues another read, expects to receive them, and reports a one-line summary of each step. This is the smallest tool that exercises the non-blocking path end to end.

`producer_consumer.c` is a fork-based load harness. It spawns a child process that writes random bytes into the driver at a configurable rate, while the parent reads them out and verifies integrity. The purpose is to exercise the circular buffer's wrap-around and the blocking path under real concurrent load.

`stress_rw.c` (evolved from Chapter 9's version) is a single-process stress tester that runs through a table of (block size, transfer count) combinations and prints aggregate timing and byte-counter statistics. The purpose is to catch performance cliffs that a single interactive test would not reveal.

All three compile with a short Makefile that we will show at the end.

### Updating `rw_myfirst` for Larger Inputs

The existing `rw_myfirst` from Chapter 9 handles text-sized transfers well but does not stress the buffer at volume. A simple extension lets it take a size argument on the command line:

```c
/* rw_myfirst_v2.c: an incremental improvement on Chapter 9's tester. */
#include <sys/types.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DEVPATH "/dev/myfirst"

static int
do_fill(size_t bytes)
{
        int fd = open(DEVPATH, O_WRONLY);
        if (fd < 0)
                err(1, "open %s", DEVPATH);

        char *buf = malloc(bytes);
        if (buf == NULL)
                err(1, "malloc %zu", bytes);
        for (size_t i = 0; i < bytes; i++)
                buf[i] = (char)('A' + (i % 26));

        size_t left = bytes;
        ssize_t n;
        const char *p = buf;
        while (left > 0) {
                n = write(fd, p, left);
                if (n < 0) {
                        if (errno == EINTR)
                                continue;
                        warn("write at %zu left", left);
                        break;
                }
                p += n;
                left -= n;
        }
        size_t wrote = bytes - left;
        printf("fill: wrote %zu of %zu\n", wrote, bytes);
        free(buf);
        close(fd);
        return (0);
}

static int
do_drain(size_t bytes)
{
        int fd = open(DEVPATH, O_RDONLY);
        if (fd < 0)
                err(1, "open %s", DEVPATH);

        char *buf = malloc(bytes);
        if (buf == NULL)
                err(1, "malloc %zu", bytes);

        size_t left = bytes;
        ssize_t n;
        char *p = buf;
        while (left > 0) {
                n = read(fd, p, left);
                if (n < 0) {
                        if (errno == EINTR)
                                continue;
                        warn("read at %zu left", left);
                        break;
                }
                if (n == 0) {
                        printf("drain: EOF at %zu left\n", left);
                        break;
                }
                p += n;
                left -= n;
        }
        size_t got = bytes - left;
        printf("drain: read %zu of %zu\n", got, bytes);
        free(buf);
        close(fd);
        return (0);
}

int
main(int argc, char *argv[])
{
        if (argc != 3) {
                fprintf(stderr, "usage: %s fill|drain BYTES\n", argv[0]);
                return (1);
        }
        size_t bytes = strtoul(argv[2], NULL, 0);
        if (strcmp(argv[1], "fill") == 0)
                return (do_fill(bytes));
        if (strcmp(argv[1], "drain") == 0)
                return (do_drain(bytes));
        fprintf(stderr, "unknown mode: %s\n", argv[1]);
        return (1);
}
```

With this tool, you can drive the driver with realistic sizes. For example:

```sh
$ ./rw_myfirst_v2 fill 4096
fill: wrote 4096 of 4096
$ sysctl dev.myfirst.0.stats.cb_used
dev.myfirst.0.stats.cb_used: 4096
$ ./rw_myfirst_v2 drain 4096
drain: read 4096 of 4096
$ sysctl dev.myfirst.0.stats.cb_used
dev.myfirst.0.stats.cb_used: 0
```

Now try to fill the buffer beyond its capacity and watch what happens at each stage.

### Why a Round-Trip Test Matters

Every serious test you write should have a *round-trip* component: write a known pattern into the driver, read it back, and compare. The pattern matters because if you write "Hello, world!" ten times you cannot tell whether the buffer got 140 bytes of "Hello, world!" or 130 or 150 or some weird interleaving. A unique per-position pattern (like `'A' + (i % 26)` above) lets you spot misalignment, missing bytes, and duplicated bytes at a glance.

Round-trip testing is especially important for circular buffers, because the wrap-around arithmetic is the thing that beginner code gets wrong. A write that pushes past the end of the underlying storage and a read that picks up from before the start are the two failure modes you most want to catch. Both appear as "the bytes I read are not the bytes I wrote", and a round-trip test makes them visible immediately.

### Building `rw_myfirst_nb`

This is the non-blocking tester. It is slightly longer than the previous file but still short enough to read in one sitting.

```c
/* rw_myfirst_nb.c: non-blocking behaviour tester for /dev/myfirst. */
#include <sys/types.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define DEVPATH "/dev/myfirst"

int
main(void)
{
        int fd, error;
        ssize_t n;
        char rbuf[128];
        struct pollfd pfd;

        fd = open(DEVPATH, O_RDWR | O_NONBLOCK);
        if (fd < 0)
                err(1, "open %s", DEVPATH);

        /* Expect EAGAIN when the buffer is empty. */
        n = read(fd, rbuf, sizeof(rbuf));
        if (n < 0 && errno == EAGAIN)
                printf("step 1: empty-read returned EAGAIN (expected)\n");
        else
                printf("step 1: UNEXPECTED read returned %zd errno=%d\n", n, errno);

        /* poll(POLLIN) with timeout 0 should show not-readable. */
        pfd.fd = fd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        error = poll(&pfd, 1, 0);
        printf("step 2: poll(POLLIN, 0) = %d revents=0x%x\n",
            error, pfd.revents);

        /* Write some bytes. */
        n = write(fd, "hello world\n", 12);
        printf("step 3: wrote %zd bytes\n", n);

        /* poll(POLLIN) should now show readable. */
        pfd.events = POLLIN;
        pfd.revents = 0;
        error = poll(&pfd, 1, 0);
        printf("step 4: poll(POLLIN, 0) = %d revents=0x%x\n",
            error, pfd.revents);

        /* Non-blocking read should now succeed. */
        memset(rbuf, 0, sizeof(rbuf));
        n = read(fd, rbuf, sizeof(rbuf));
        if (n > 0) {
                rbuf[n] = '\0';
                printf("step 5: read %zd bytes: %s", n, rbuf);
        } else
                printf("step 5: UNEXPECTED read returned %zd errno=%d\n",
                    n, errno);

        close(fd);
        return (0);
}
```

Expected output against Stage 3 (non-blocking support) is:

```text
step 1: empty-read returned EAGAIN (expected)
step 2: poll(POLLIN, 0) = 0 revents=0x0
step 3: wrote 12 bytes
step 4: poll(POLLIN, 0) = 1 revents=0x41
step 5: read 12 bytes: hello world
```

The `0x41` in step 4 is `POLLIN | POLLRDNORM`, which is exactly what our `d_poll` handler sets when the buffer has live bytes.

If step 1 fails (i.e. `read(2)` returns `0` instead of `-1`/`EAGAIN`), your driver is still running the Stage 2 semantics. Go back and add the `IO_NDELAY` check in the handlers.

If step 2 succeeds with `revents != 0`, your `d_poll` is wrongly reporting readable on an empty buffer. Check the condition in `myfirst_poll`.

If step 4 returns zero (i.e. `poll(2)` did not find the descriptor readable), your `d_poll` is not reflecting the buffer state correctly, or the `selwakeup` call is missing from the write path.

These are the three most common non-blocking bugs. The tester catches all of them in under fifty lines of output.

### Building `producer_consumer.c`

This is the fork-based load harness. The shape is straightforward: fork a child that writes, have the parent read, and compare what comes out against what went in.

```c
/* producer_consumer.c: a two-process load test for /dev/myfirst. */
#include <sys/types.h>
#include <sys/wait.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DEVPATH         "/dev/myfirst"
#define TOTAL_BYTES     (1024 * 1024)
#define BLOCK           4096

static uint32_t
checksum(const char *p, size_t n)
{
        uint32_t s = 0;
        for (size_t i = 0; i < n; i++)
                s = s * 31u + (uint8_t)p[i];
        return (s);
}

static int
do_writer(void)
{
        int fd = open(DEVPATH, O_WRONLY);
        if (fd < 0)
                err(1, "writer: open");

        char *buf = malloc(BLOCK);
        if (buf == NULL)
                err(1, "writer: malloc");

        size_t written = 0;
        uint32_t sum = 0;
        while (written < TOTAL_BYTES) {
                size_t left = TOTAL_BYTES - written;
                size_t block = left < BLOCK ? left : BLOCK;
                for (size_t i = 0; i < block; i++)
                        buf[i] = (char)((written + i) & 0xff);
                sum += checksum(buf, block);

                const char *p = buf;
                size_t remain = block;
                while (remain > 0) {
                        ssize_t n = write(fd, p, remain);
                        if (n < 0) {
                                if (errno == EINTR)
                                        continue;
                                warn("writer: write");
                                close(fd);
                                return (1);
                        }
                        p += n;
                        remain -= n;
                }
                written += block;
        }

        printf("writer: %zu bytes, checksum 0x%08x\n", written, sum);
        close(fd);
        free(buf);
        return (0);
}

static int
do_reader(void)
{
        int fd = open(DEVPATH, O_RDONLY);
        if (fd < 0)
                err(1, "reader: open");

        char *buf = malloc(BLOCK);
        if (buf == NULL)
                err(1, "reader: malloc");

        size_t got = 0;
        uint32_t sum = 0;
        int mismatches = 0;
        while (got < TOTAL_BYTES) {
                ssize_t n = read(fd, buf, BLOCK);
                if (n < 0) {
                        if (errno == EINTR)
                                continue;
                        warn("reader: read");
                        break;
                }
                if (n == 0) {
                        /* Only reached if driver signals EOF. */
                        printf("reader: EOF at %zu\n", got);
                        break;
                }
                for (ssize_t i = 0; i < n; i++) {
                        if ((uint8_t)buf[i] != (uint8_t)((got + i) & 0xff))
                                mismatches++;
                }
                sum += checksum(buf, n);
                got += n;
        }

        printf("reader: %zu bytes, checksum 0x%08x, mismatches %d\n",
            got, sum, mismatches);
        close(fd);
        free(buf);
        return (mismatches == 0 ? 0 : 2);
}

int
main(void)
{
        pid_t pid = fork();
        if (pid < 0)
                err(1, "fork");
        if (pid == 0) {
                /* child: writer */
                _exit(do_writer());
        }
        /* parent: reader */
        int rc = do_reader();
        int status;
        waitpid(pid, &status, 0);
        int wexit = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        printf("exit: reader=%d writer=%d\n", rc, wexit);
        return (rc || wexit);
}
```

The test works best against Stage 3 or Stage 4 of the chapter. Against Stage 2 (no blocking), the writer will receive short writes and the reader will occasionally see zero-byte reads, and the total bytes transferred may be less than `TOTAL_BYTES`. Against Stage 3, both sides block and unblock at the right times, the test runs to completion, and the two checksums match.

A successful run looks like:

```sh
$ ./producer_consumer
writer: 1048576 bytes, checksum 0x12345678
reader: 1048576 bytes, checksum 0x12345678, mismatches 0
exit: reader=0 writer=0
```

Mismatches are the killer. If the writer's checksum matches the reader's but mismatches are non-zero, it means a byte drifted in position during the round trip (likely a wrap-around bug). If the checksums differ, a byte was lost or duplicated (likely a locking bug). If the test hangs forever, the blocking path's condition never becomes true (likely a missing `wakeup`).

### Using `dd(1)` for Volume Tests

The base-system `dd(1)` is the fastest way to push volume through the driver without writing any new code. A few patterns are especially useful.

**Pattern 1: writer-only.** Push a large amount of data into the driver while a reader keeps up.

```sh
$ dd if=/dev/myfirst of=/dev/null bs=4k &
$ dd if=/dev/zero of=/dev/myfirst bs=4k count=100000
```

This produces 400 MB of traffic through the driver. Watch `sysctl dev.myfirst.0.stats.bytes_written` grow, and compare it against `bytes_read`; the difference is roughly the buffer fill level.

**Pattern 2: rate-limited.** Some tests want to stress the driver at a steady rate rather than at maximum throughput. Use `rate` or the GNU `pv(1)` utility (available as `ports/sysutils/pv`) to cap:

```sh
$ pv -L 10m < /dev/zero | dd of=/dev/myfirst bs=4k
```

This caps the write rate at 10 MB/s. A slower rate lets you observe the buffer's fill level in `sysctl` and see the blocking path engage when the rate approaches the consumer's rate.

**Pattern 3: full-block.** As mentioned in Section 4, default `dd` does not loop on short reads. Use `iflag=fullblock` to make it do so:

```sh
$ dd if=/dev/myfirst of=/tmp/out bs=4k count=100 iflag=fullblock
```

Without `iflag=fullblock`, the output file might be shorter than the requested 400 KB because of short reads.

### Using `hexdump(1)` to Verify Content

`hexdump(1)` is the right tool for verifying the *content* of what the driver delivers. If you write a known byte sequence and want to confirm it comes back intact, `hexdump` shows you.

```sh
$ printf 'ABCDEFGH' > /dev/myfirst
$ hexdump -C /dev/myfirst
00000000  41 42 43 44 45 46 47 48                           |ABCDEFGH|
$
```

The `hexdump -C` output is the canonical "here are the bytes and their ASCII interpretation" format. It is especially useful when the driver is emitting binary data that text-based tools cannot display.

### Using `truss(1)` to See Syscall Traffic

`truss(1)` traces the system calls made by a process. Running a test under `truss` shows you exactly what each `read(2)` and `write(2)` returned, including partial transfers and error codes.

```sh
$ truss -t read,write -o /tmp/trace ./rw_myfirst_nb
$ head /tmp/trace
read(3,0x7fffffffeca0,128)                       ERR#35 'Resource temporarily unavailable'
write(3,"hello world\n",12)                      = 12 (0xc)
read(3,0x7fffffffeca0,128)                       = 12 (0xc)
...
```

ERR#35 is `EAGAIN`. Seeing it confirms that the non-blocking path is engaging. Running `producer_consumer` under `truss` shows the pattern of short writes and short reads very clearly; it is a good diagnostic for debugging buffer sizing issues.

A related tool is `ktrace(1)` / `kdump(1)`, which produces a more detailed and decoded trace, at the cost of being a bit more verbose. Either is fine for this level of work.

### Using `sysctl(8)` to Watch State Live

The sysctl tree `dev.myfirst.0.stats.*` is the driver's live state. Watching it in real time during a test tells you a great deal about what the driver is doing.

```sh
$ while true; do
    clear
    sysctl dev.myfirst.0.stats | egrep 'cb_|bytes_'
    sleep 1
  done
```

Run this in one terminal while a test runs in another. You will see `cb_used` rise as the writer gets ahead, fall as the reader catches up, and oscillate around some steady-state level. The byte counters only ever increase. A stalled test shows up as frozen counters.

### Using `vmstat -m` to Watch Memory

If you suspect a leak (maybe you forgot `cbuf_destroy` in the error path of `attach`), `vmstat -m` shows it:

```sh
$ vmstat -m | grep cbuf
         cbuf     1      4K        1  4096
```

After `kldunload`:

```sh
$ vmstat -m | grep cbuf
$
```

The tag should disappear entirely when the driver is unloaded. If a count is non-zero, something still holds the allocation. This is the kind of regression you want to catch immediately; it gets worse silently over time.

### Building the Test Kit

Here is a Makefile that builds all the userland test programs at once. Place it in `examples/part-02/ch10-handling-io-efficiently/userland/`:

```make
# Makefile for Chapter 10 userland testers.

PROGS= rw_myfirst_v2 rw_myfirst_nb producer_consumer stress_rw cb_test

.PHONY: all
all: ${PROGS}

CFLAGS?= -O2 -Wall -Wextra -Wno-unused-parameter

rw_myfirst_v2: rw_myfirst_v2.c
	${CC} ${CFLAGS} -o $@ $<

rw_myfirst_nb: rw_myfirst_nb.c
	${CC} ${CFLAGS} -o $@ $<

producer_consumer: producer_consumer.c
	${CC} ${CFLAGS} -o $@ $<

stress_rw: stress_rw.c
	${CC} ${CFLAGS} -o $@ $<

cb_test: ../cbuf-userland/cbuf.c ../cbuf-userland/cb_test.c
	${CC} ${CFLAGS} -I../cbuf-userland -o $@ $^

.PHONY: clean
clean:
	rm -f ${PROGS}
```

Running `make` builds all four tools. `make cb_test` builds only the standalone `cbuf` test. Keep the two userland directories (`cbuf-userland/` for the buffer, `userland/` for the driver testers) separate; the first one is the prerequisite for the later stages, and building it in isolation mirrors the order we introduced them in the chapter.

### A Consolidated Test Plan

With the tools in place, here is a test plan you can run against each stage of the driver. Run each pass after loading the corresponding `myfirst.ko`.

**Stage 2 (circular buffer, no blocking):**

1. `./rw_myfirst_v2 fill 4096; sysctl dev.myfirst.0.stats.cb_used` should report 4096.
2. `./rw_myfirst_v2 fill 4097` should show a short write (wrote 4096 of 4097).
3. `./rw_myfirst_v2 drain 2048; sysctl dev.myfirst.0.stats.cb_used` should report 2048.
4. `./rw_myfirst_v2 fill 2048; sysctl dev.myfirst.0.stats.cb_used` should report 4096, but `cb_head` should be non-zero (proving wrap-around worked).
5. `dd if=/dev/myfirst of=/dev/null bs=4k`: should drain 4096 bytes and then return zero.
6. `producer_consumer` with `TOTAL_BYTES = 8192`: should complete successfully.

**Stage 3 (blocking and non-blocking support):**

1. `cat /dev/myfirst &` should block.
2. `echo hi > /dev/myfirst` should produce output in the `cat` terminal.
3. `kill -INT %1` should unblock `cat` cleanly.
4. `./rw_myfirst_nb` should print the six-line output above.
5. `producer_consumer` with `TOTAL_BYTES = 1048576`: should complete with no mismatches and matching checksums.

**Stage 4 (poll support, refactored helpers):**

All Stage 3 tests, plus:

1. `./rw_myfirst_nb` step 4 should show `revents=0x41` (POLLIN|POLLRDNORM).
2. A small program that opens one descriptor read-only non-blocking, registers it with `poll(POLLIN)` with timeout -1, and calls `write` from the same process on a second descriptor: the `poll` should return promptly with `POLLIN` set.
3. `dd if=/dev/zero of=/dev/myfirst bs=1m count=10 &` paired with `dd if=/dev/myfirst of=/dev/null bs=4k`: should move 10 MB with no errors, roughly in the time the slower side takes.

This plan is by no means exhaustive. The Labs section later in the chapter gives you a deeper sequence. But these are the smoke tests: run them after every non-trivial change, and if they pass, you have not broken anything fundamental.

### Debugging When a Test Fails

When a test fails, the sequence of inspection is usually:

1. **`dmesg | tail -100`**: check for kernel warnings, panics, or your own `device_printf` output. If the kernel is complaining about a locking violation or a `witness` warning, the problem is visible here before you do anything else.
2. **`sysctl dev.myfirst.0.stats`**: compare the current values to what they should be. If `cb_used` is non-zero but no one is holding a descriptor open, something went wrong with the shutdown path.
3. **`truss -t read,write,poll -f`**: run the failing tester under `truss` and see the syscall returns. A spurious `EAGAIN` (or absence thereof) shows up immediately.
4. **`ktrace`**: if `truss` is not enough, `ktrace -di ./test; kdump -f ktrace.out` gives a deeper view including signals.
5. **Add `device_printf` to the driver**: sprinkle one-line traces at the top and bottom of each handler, then reproduce the test. This is the fallback, and it is sometimes the only way to see what the driver is doing at the moments the user-side tools do not capture.

Be careful with the last step. Every `device_printf` goes through the kernel's log buffer, which is itself a finite circular buffer. Dropping a `device_printf` into the `cbuf_write` function that runs every byte will melt the log. Start with one log line per I/O call and increase only if needed.

### Wrapping Up Section 6

You now have a test kit that can exercise every non-trivial behaviour the driver promises. `rw_myfirst_v2` covers sized reads and writes and round-trip correctness. `rw_myfirst_nb` covers the non-blocking path and the `poll(2)` contract. `producer_consumer` covers concurrent two-party load with content verification. `dd`, `cat`, `hexdump`, `truss`, `sysctl`, and `vmstat -m` together provide observability into the driver's internal state.

None of these tools are new or exotic. They are standard FreeBSD base-system utilities, and short pieces of code that you can type in an afternoon. The combination is enough to catch most driver bugs before they reach anyone else's hands. The next section takes the driver you have just finished testing and prepares its code shape for the concurrency work in Chapter 11.

## Section 7: Refactoring and Preparing for Concurrency

The driver works. It buffers, it blocks, it poll-reports correctly, and the user-space tests in Section 6 confirm that bytes flow correctly under realistic load. What we have not yet done is shape the code for the work Chapter 11 will do. This section is the bridge: it identifies the places in the current source that will need attention from a real concurrency standpoint, refactors the buffer access into a tight set of helpers, and finishes by making the driver as honest as possible about its own state.

We are not introducing new locking primitives here. Chapter 11 will explore that material at length, including the alternatives to a single mutex (sleepable locks, sx locks, rwlocks, lock-free patterns), the verification tools (`WITNESS`, `INVARIANTS`), and the rules around interrupt context, sleep, and lock ordering. What we are doing in Section 7 is making the *shape* of the code such that those tools can be applied cleanly when the time comes.

### Identifying Potential Race Conditions

A "race condition" in driver code is any place where the correctness of the code depends on the order in which two threads execute, where the order is not enforced by anything in the driver. The Stage 4 driver has the right *machinery* in place (a mutex, a sleep channel, sleep-with-mutex semantics through `mtx_sleep`) and the I/O handlers respect it. But there are still places where a careful audit is worthwhile.

Let us walk through the data structures and ask, of each shared field, "who reads it, who writes it, what protects access?"

**`sc->cb` (the circular buffer).** Read by `myfirst_read`, written by `myfirst_write`, read by `myfirst_poll`, read by the two sysctl handlers (`cb_used` and `cb_free`), read by `myfirst_detach` (implicitly through `cbuf_destroy`). Protected by `sc->mtx` everywhere it is touched. *Looks safe.*

**`sc->bytes_read`, `sc->bytes_written`.** Updated by the two I/O handlers under `sc->mtx`. Read by sysctl directly through `SYSCTL_ADD_U64` (no handler interposed). The sysctl read is a single 64-bit load on most architectures, which is a torn-read risk on some 32-bit platforms but is atomic on amd64 and arm64. *Mostly safe; see torn-read note below.*

**`sc->open_count`, `sc->active_fhs`.** Updated under `sc->mtx`. Read by sysctl directly. Same torn-read consideration.

**`sc->is_attached`.** Read by every handler at entry, set by attach (without lock, before `make_dev`), cleared by detach (under lock). The unlocked write at attach time is safe because no one else can see the device yet. The locked clear at detach time is correctly ordered with the wakeup. *Looks safe.*

**`sc->cdev`, `sc->cdev_alias`.** Set by attach, cleared by detach. Once attach is done, these are stable for the lifetime of the device. The handlers reach the softc through `dev->si_drv1` (set during attach) and never dereference these directly during I/O. *Safe by construction.*

**`sc->rsel`, `sc->wsel`.** The `selinfo` machinery is internally locked (it uses the kernel's `selspinlock` and per-mutex `knlist` if you initialise one). For pure `select(2)`/`poll(2)` use, the `selrecord` and `selwakeup` calls handle their own concurrency. *Safe.*

**`sc->open_count` and friends, again.** The torn-read note above is worth being explicit about. On 32-bit platforms (i386, armv7), a 64-bit field can be split across two memory operations, and a concurrent write can yield a read that contains the high half of one value and the low half of another (a "torn read"). The chapter is targeting amd64 where this is not an issue, but it is the kind of thing a real driver should think about. The fix, if needed, is to add a sysctl handler (like the `cb_used` one) that takes the mutex around the load.

The audit above gives a clean bill of health. The bigger refactoring opportunities are not race conditions but *code shape*: places where the buffer logic is mixed in with the I/O logic, where helper functions would clarify intent, and where Chapter 11 can introduce new lock classes without touching the I/O handlers.

### The Refactor: Pulling Buffer Access into Helpers

The Stage 3 / Stage 4 handlers contain a fair amount of locking and book-keeping inline. Let us extract that into a small set of helpers. The goal is two-fold: the I/O handlers become obviously correct, and Chapter 11 can substitute different locking strategies into the helpers without touching `myfirst_read` or `myfirst_write`.

Define the following helpers, all in `myfirst.c` (or in a new file `myfirst_buf.c` if you want clearer separation):

```c
/* Read up to "n" bytes from the cbuf into "dst".  Returns count moved. */
static size_t
myfirst_buf_read(struct myfirst_softc *sc, void *dst, size_t n)
{
        size_t got;

        mtx_assert(&sc->mtx, MA_OWNED);
        got = cbuf_read(&sc->cb, dst, n);
        sc->bytes_read += got;
        return (got);
}

/* Write up to "n" bytes from "src" into the cbuf.  Returns count moved. */
static size_t
myfirst_buf_write(struct myfirst_softc *sc, const void *src, size_t n)
{
        size_t put;

        mtx_assert(&sc->mtx, MA_OWNED);
        put = cbuf_write(&sc->cb, src, n);
        sc->bytes_written += put;
        return (put);
}

/* Wait, with PCATCH, until the cbuf is non-empty or the device tears down. */
static int
myfirst_wait_data(struct myfirst_softc *sc, int ioflag, ssize_t nbefore,
    struct uio *uio)
{
        int error;

        mtx_assert(&sc->mtx, MA_OWNED);
        while (cbuf_used(&sc->cb) == 0) {
                if (uio->uio_resid != nbefore)
                        return (-1);            /* signal caller to break */
                if (ioflag & IO_NDELAY)
                        return (EAGAIN);
                error = mtx_sleep(&sc->cb, &sc->mtx, PCATCH, "myfrd", 0);
                if (error != 0)
                        return (error);
                if (!sc->is_attached)
                        return (ENXIO);
        }
        return (0);
}

/* Wait, with PCATCH, until the cbuf has free space or the device tears down. */
static int
myfirst_wait_room(struct myfirst_softc *sc, int ioflag, ssize_t nbefore,
    struct uio *uio)
{
        int error;

        mtx_assert(&sc->mtx, MA_OWNED);
        while (cbuf_free(&sc->cb) == 0) {
                if (uio->uio_resid != nbefore)
                        return (-1);            /* signal caller to break */
                if (ioflag & IO_NDELAY)
                        return (EAGAIN);
                error = mtx_sleep(&sc->cb, &sc->mtx, PCATCH, "myfwr", 0);
                if (error != 0)
                        return (error);
                if (!sc->is_attached)
                        return (ENXIO);
        }
        return (0);
}
```

The `mtx_assert(&sc->mtx, MA_OWNED)` calls are a tiny but valuable safety net. If a future caller forgets to acquire the lock before calling one of these helpers, the assertion fires (in a `WITNESS` kernel). Once you trust the helpers, you can stop thinking about the lock at the call sites.

The four helpers together cover everything the I/O handlers need from the buffer abstraction: reading bytes, writing bytes, waiting for data, waiting for room. Each helper takes the mutex by reference and asserts that it is held. None of them lock or unlock.

With the helpers defined, the I/O handlers shrink considerably:

```c
static int
myfirst_read(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct myfirst_softc *sc = dev->si_drv1;
        struct myfirst_fh *fh;
        char bounce[MYFIRST_BOUNCE];
        size_t take, got;
        ssize_t nbefore;
        int error;

        error = devfs_get_cdevpriv((void **)&fh);
        if (error != 0)
                return (error);
        if (sc == NULL || !sc->is_attached)
                return (ENXIO);

        nbefore = uio->uio_resid;
        while (uio->uio_resid > 0) {
                mtx_lock(&sc->mtx);
                error = myfirst_wait_data(sc, ioflag, nbefore, uio);
                if (error != 0) {
                        mtx_unlock(&sc->mtx);
                        return (error == -1 ? 0 : error);
                }
                take = MIN((size_t)uio->uio_resid, sizeof(bounce));
                got = myfirst_buf_read(sc, bounce, take);
                fh->reads += got;
                mtx_unlock(&sc->mtx);

                wakeup(&sc->cb);
                selwakeup(&sc->wsel);

                error = uiomove(bounce, got, uio);
                if (error != 0)
                        return (error);
        }
        return (0);
}

static int
myfirst_write(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct myfirst_softc *sc = dev->si_drv1;
        struct myfirst_fh *fh;
        char bounce[MYFIRST_BOUNCE];
        size_t want, put, room;
        ssize_t nbefore;
        int error;

        error = devfs_get_cdevpriv((void **)&fh);
        if (error != 0)
                return (error);
        if (sc == NULL || !sc->is_attached)
                return (ENXIO);

        nbefore = uio->uio_resid;
        while (uio->uio_resid > 0) {
                mtx_lock(&sc->mtx);
                error = myfirst_wait_room(sc, ioflag, nbefore, uio);
                if (error != 0) {
                        mtx_unlock(&sc->mtx);
                        return (error == -1 ? 0 : error);
                }
                room = cbuf_free(&sc->cb);
                mtx_unlock(&sc->mtx);

                want = MIN((size_t)uio->uio_resid, sizeof(bounce));
                want = MIN(want, room);
                error = uiomove(bounce, want, uio);
                if (error != 0)
                        return (error);

                mtx_lock(&sc->mtx);
                put = myfirst_buf_write(sc, bounce, want);
                fh->writes += put;
                mtx_unlock(&sc->mtx);

                wakeup(&sc->cb);
                selwakeup(&sc->rsel);
        }
        return (0);
}
```

Each I/O handler now does the same three things in the same order: take the lock, ask the helper for state, release the lock, do the copy, take the lock again, update state, release, wake. The pattern is clear enough that a future reader can verify the locking discipline at a glance.

The error code `-1` returned by the wait helpers is a small convention: "no error to report, but the loop should break and the caller should return 0." Using `-1` (which is not a valid errno) makes the convention obvious without adding a third out-parameter. It is local to the driver and never escapes to user space.

### Documenting the Locking Strategy

A driver this size benefits from a one-paragraph comment near the top of the file explaining the locking discipline. The comment is for the next person who reads the code, and it is your future self in three months. Add this near the `struct myfirst_softc` declaration:

```c
/*
 * Locking strategy.
 *
 * sc->mtx protects:
 *   - sc->cb (the circular buffer's internal state)
 *   - sc->bytes_read, sc->bytes_written
 *   - sc->open_count, sc->active_fhs
 *   - sc->is_attached
 *
 * Locking discipline:
 *   - The mutex is acquired with mtx_lock and released with mtx_unlock.
 *   - mtx_sleep(&sc->cb, &sc->mtx, PCATCH, ...) is used to block while
 *     waiting on buffer state.  wakeup(&sc->cb) is the matching call.
 *   - The mutex is NEVER held across uiomove(9), copyin(9), or copyout(9),
 *     all of which may sleep.
 *   - The mutex is held when calling cbuf_*() helpers; the cbuf module is
 *     intentionally lock-free by itself and relies on the caller for safety.
 *   - selwakeup(9) and wakeup(9) are called with the mutex DROPPED, after
 *     the state change that warrants the wake.
 */
```

That comment is enough for Chapter 11 to either follow the same convention or to deliberately change it. A driver that explains its own rules makes future maintenance easier; a driver that does not explain its rules leaves every future reader to infer them from the source, which is slow and error-prone.

### Splitting `cbuf` Out of `myfirst.c`

In Stage 2 and Stage 3, the `cbuf` source lived alongside `myfirst.c` in the same module directory but in its own `.c` file. The Makefile is updated to compile both:

```make
KMOD=    myfirst
SRCS=    myfirst.c cbuf.c
SRCS+=   device_if.h bus_if.h

.include <bsd.kmod.mk>
```

Two minor pieces are worth noting.

The first is that `cbuf.c` declares its own `MALLOC_DEFINE`. Each `MALLOC_DEFINE` for the same tag in the same module would be a duplicate definition; we therefore put the declaration in exactly one source file (`cbuf.c`) and an `extern` declaration in `cbuf.h` if needed. In our setup, the tag is local to `cbuf.c` and no external use is needed.

The second is that `cbuf.c` does not need any of the `myfirst` headers. It is a self-contained library that the driver happens to use. If you ever wanted to share `cbuf` with a second driver, you could pull it out into its own KLD or into `/usr/src/sys/sys/cbuf.h` and `/usr/src/sys/kern/subr_cbuf.c` (a hypothetical placement). The discipline of keeping `cbuf` self-contained makes that possible.

### Naming Conventions

A small but useful pattern: name your buffer-related fields and functions consistently. We have used `sc->cb` for the buffer, `cbuf_*` for buffer functions, `myfirst_buf_*` for the driver's wrappers. The pattern lets a reader scan the code and instantly know whether a function is touching the raw buffer (`cbuf_*`) or going through the locked driver wrappers (`myfirst_buf_*`).

Avoid mixing styles. Calling the buffer `sc->ring` in some places and `sc->cb` in others, or `cbuf_get` and `cbuf_read`, makes the code harder to skim. Pick one set of names and use them throughout.

### Defending Against Buffer-Size Surprises

The `MYFIRST_BUFSIZE` macro determines the capacity of the ring. Right now it is hard-coded to 4096. There is nothing wrong with that, but a `sysctl` knob (read-only) that exposes the value, plus a `module_param`-style override at module load time, would make the driver more usable in tests without needing to recompile.

Here is the pattern for a load-time override using `TUNABLE_INT`:

```c
static int myfirst_bufsize = MYFIRST_BUFSIZE;
TUNABLE_INT("hw.myfirst.bufsize", &myfirst_bufsize);
SYSCTL_INT(_hw_myfirst, OID_AUTO, bufsize, CTLFLAG_RDTUN,
    &myfirst_bufsize, 0, "Default buffer size for new myfirst attaches");
```

`TUNABLE_INT` reads the value from the kernel environment at boot or `kldload` time. A user can set it from the loader prompt (`set hw.myfirst.bufsize=8192`) or by running `kenv hw.myfirst.bufsize=8192` before `kldload`. The `CTLFLAG_RDTUN` flag indicates "read-only at runtime, but tunable at load time." After load, `sysctl hw.myfirst.bufsize` shows the chosen value.

Then in `myfirst_attach`, use `myfirst_bufsize` instead of `MYFIRST_BUFSIZE` in the `cbuf_init` call. The change is small but useful: now you can experiment with different buffer sizes without rebuilding the module.

### Goals for the Next Milestone

Where Chapter 11 takes the driver:

- The single mutex you have today protects everything. Chapter 11 will discuss whether a single lock is the right design under heavy contention, whether sleepable locks (`sx_*`) would be more appropriate, and how to reason about lock ordering when multiple subsystems get involved.
- The blocking path uses `mtx_sleep`, which is the right primitive for this kind of work. Chapter 11 will introduce `cv_wait(9)` (condition variables) as a more structured alternative for some patterns, and discuss when each is preferable.
- The wake-up strategy uses `wakeup(9)` (wake everyone). Chapter 11 will discuss `wakeup_one(9)` and the thundering-herd problem, and when each is appropriate.
- The cbuf is intentionally not thread-safe by itself. Chapter 11 will revisit this decision and discuss the tradeoffs of building locking *into* the data structure versus leaving it to the caller.
- The detach path's "wait for descriptors to close" rule is conservative. Chapter 11 will discuss alternative strategies (forced revocation, reference counting at the cdev level, the `destroy_dev_drain(9)` mechanism) for drivers that need to detach despite open descriptors.

You do not need to know any of this material yet. The point is that the current code's *shape* is what makes those topics approachable in Chapter 11. You can swap the mutex for an `sx` lock without touching the helpers' signatures. You can swap `wakeup` for `wakeup_one` with one-line changes. You can introduce a per-reader sleep channel without restructuring the I/O handlers. The refactor pays off as soon as you start asking the next chapter's questions.

### A Reading Order for the Next Chapter

When you start Chapter 11, three files in `/usr/src/sys` will repay careful reading.

`/usr/src/sys/kern/subr_sleepqueue.c` is where `mtx_sleep`, `tsleep`, and `wakeup` are implemented. Read it once for context. The implementation is more elaborate than the man pages suggest, but the core of it (chan-keyed sleep queues, atomic dequeue on wake) is straightforward.

`/usr/src/sys/sys/sx.h` and `/usr/src/sys/kern/kern_sx.c` together explain the sleepable-shared-exclusive lock. We mentioned `sx` above as an alternative to `mtx`; reading the actual implementation is the best way to understand the tradeoffs.

`/usr/src/sys/sys/condvar.h` and `/usr/src/sys/kern/kern_condvar.c` document the `cv_wait` family of condition-variable primitives. Like `mtx_sleep`, they build on the kernel's sleep-queue machinery in `subr_sleepqueue.c`, but they expose a distinct structured API where each wait point has its own named `struct cv` instead of an arbitrary address as the channel. Chapter 11 will explain when to prefer each, and why a dedicated `struct cv` is often the cleaner choice for a well-defined wait condition.

These are not required reading; they are the next step on a long path you are clearly already on.

### Wrapping Up Section 7

The driver is now in the shape Chapter 11 wants. The buffer abstraction is in its own file, exercised in userland, and called from the driver through a small set of locked wrappers. The locking strategy is documented in a comment that names exactly what the mutex protects and what the rules are. The blocking path is correct, the non-blocking path is correct, the poll path is correct, and the detach path correctly waits for and wakes any sleepers.

Most of what you do in Chapter 11 will be additive to this base, not a rewrite of it. The patterns we have built (lock around state changes, sleep with the mutex as interlock, wake on every transition) are the same patterns the rest of the kernel uses. The vocabulary is the same, the primitives are the same, the discipline is the same. You are close to being able to read most character-device drivers in the tree without help.

Before we move on to the chapter's supplementary topics and the labs, take a moment to look at your own source. The Stage 4 driver should be roughly 500 lines of code (`myfirst.c`) plus about 110 lines of `cbuf.c` and 20 lines of `cbuf.h`. The total is small, the layering is clean, and almost every line is doing something specific. That density is what well-shaped driver code looks like.

## Section 8: Three Supplementary Topics

This section covers three topics that often appear alongside buffered I/O in the real world. Each one is large enough to fill an entire chapter on its own; we are not going to do that. Instead we are going to introduce each at the level a reader of this book needs in order to recognise the pattern, talk about it sensibly, and know where to look when the time comes to use it. The deeper treatments come later, in the chapters where each topic is the main subject.

The three topics are: `d_mmap(9)` for letting user space map a kernel buffer; zero-copy considerations and what they really mean; and the readahead and write-coalescing patterns used by high-throughput drivers.

### Topic 1: `d_mmap(9)` and Mapping a Kernel Buffer

`d_mmap(9)` is the character-device callback that the kernel invokes when a user-space program calls `mmap(2)` on `/dev/myfirst`. The handler's job is to translate a *file offset* into a *physical address* the VM system can map into the user's process. The signature is:

```c
typedef int d_mmap_t(struct cdev *dev, vm_ooffset_t offset, vm_paddr_t *paddr,
                     int nprot, vm_memattr_t *memattr);
```

For each page-sized chunk the user wants to map, the kernel calls `d_mmap` with `offset` set to the byte offset within the device. The handler computes the physical address of the corresponding page and stores it through `*paddr`. It can also adjust the memory attributes through `*memattr` (caching, write-combining, and so on). Returning a non-zero error code tells the kernel "this offset cannot be mapped"; returning `0` indicates success.

The reason we are introducing `d_mmap` here is that it is the lightweight cousin of buffered I/O. With `read(2)` and `write(2)`, every byte is copied across the trust boundary on each call. With `mmap(2)` followed by direct memory access, the bytes are visible to user space without any explicit copy. A user-space program reads from or writes to the mapped region exactly as if it were ordinary memory, and the kernel's buffer is the same bytes the user sees.

This pattern is appealing for a small but important class of devices. A frame buffer, a DMA-mapped device buffer, a shared-memory event queue: each of these benefits from being mapped directly so that user code can manipulate the bytes without ever entering the kernel. The classical example in `/usr/src/sys/dev/mem/memdev.c` (with the architecture-specific `memmmap` function under each `arch` directory) maps `/dev/mem` so that privileged user processes can read or write physical memory pages.

For a learning driver like ours, the goal is more modest: let `mmap(2)` see the same circular buffer that `read(2)` and `write(2)` use. The user can then read out the buffer without going through the syscall path. We will not extend the driver to support writes through `mmap` (that would require careful handling of cache coherency and concurrent updates with the syscall path), but a read-only mapping is a useful capability to add.

#### A Minimal `d_mmap` Implementation

The implementation is short:

```c
static int
myfirst_mmap(struct cdev *dev, vm_ooffset_t offset, vm_paddr_t *paddr,
    int nprot, vm_memattr_t *memattr)
{
        struct myfirst_softc *sc = dev->si_drv1;

        if (sc == NULL || !sc->is_attached)
                return (ENXIO);
        if ((nprot & VM_PROT_WRITE) != 0)
                return (EACCES);
        if (offset >= sc->cb.cb_size)
                return (-1);
        *paddr = vtophys((char *)sc->cb.cb_data + (offset & ~PAGE_MASK));
        return (0);
}
```

Add `.d_mmap = myfirst_mmap,` to the `cdevsw`. The handler does four things in sequence.

First, it checks that the device is still attached. A user that has held an `mmap` on a torn-down driver should see `ENXIO`, not a kernel panic.

Second, it refuses write mappings. Allowing `PROT_WRITE` would let user space modify the buffer concurrently with the read and write handlers, which would race with the cbuf's invariants. A read-only mapping is enough for our learning purposes; a real driver that wants writable mappings has to do considerably more work to keep the cbuf consistent.

Third, it bounds the offset. The user could request `offset = 1 << 30`, far past the end of the buffer; the handler returns `-1` to refuse. (Returning `-1` tells the kernel "no valid address for this offset"; the kernel treats this as the end of the mappable region.)

Fourth, it computes the physical address with `vtophys(9)`. `vtophys` translates a kernel virtual address into the corresponding physical address for a single page. The buffer was allocated with `malloc(9)`, which returns *virtually* contiguous memory; for an allocation that fits in one page (our `MYFIRST_BUFSIZE` of 4096 bytes on a 4 KB page machine) this is trivially also physically contiguous, and one `vtophys` is enough. For larger buffers, each page must be looked up individually, because `malloc(9)` does not promise cross-page physical contiguity. The expression `(offset & ~PAGE_MASK)` rounds the caller's offset down to the page boundary so that `vtophys` is called on the correct page base; the kernel then takes care of applying the intra-page offset from the user's `mmap` call. A production driver whose buffer may span more than one page should walk the allocation page by page, or switch to `contigmalloc(9)` when physical contiguity is actually required.

#### Caveats and Limitations

A few important caveats apply to this minimal implementation.

`vtophys` works for memory allocated by `malloc(9)` only when each page of the allocation is contiguous in physical memory. Small allocations (under one page) are always contiguous. Larger allocations made with `malloc(9)` are *virtually* contiguous but not necessarily physically contiguous; the handler would need to compute the per-page physical address rather than assuming linearity. For Chapter 10's 4 KB buffer (which fits in a single page) the simple form works.

For genuinely large buffers, the right primitive is `contigmalloc(9)` (contiguous physical memory) or `dev_pager_*` functions to provide a custom pager. Both belong in later chapters where we discuss VM details properly.

The mapping is read-only. A `PROT_WRITE` request will fail with `EACCES`. Allowing writes would require either a way to invalidate user mappings when the cbuf indices change (impractical for a circular buffer), or a fundamentally different design where the user's writes drive the buffer directly. Neither is appropriate for a learning chapter.

Finally, mapping the cbuf does *not* let user space see a coherent stream of bytes the way a `read` does. The mapping shows the *raw* underlying memory, including bytes outside the live region (which may be stale or zero) and ignoring the head/used indices. A user that reads from the mapping needs to consult `sysctl dev.myfirst.0.stats.cb_used` and `cb_used` to know where the live region starts and ends. This is intentional: `mmap` is a low-level mechanism that exposes raw memory, and any structured interpretation has to be layered on top.

#### A Small `mmap` Tester

A user-space program that maps the buffer and walks it looks like:

```c
/* mmap_myfirst.c: map the myfirst buffer read-only and dump it. */
#include <sys/mman.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define DEVPATH "/dev/myfirst"
#define BUFSIZE 4096

int
main(void)
{
        int fd = open(DEVPATH, O_RDONLY);
        if (fd < 0) { perror("open"); return (1); }

        char *map = mmap(NULL, BUFSIZE, PROT_READ, MAP_SHARED, fd, 0);
        if (map == MAP_FAILED) { perror("mmap"); close(fd); return (1); }

        printf("first 64 bytes:\n");
        for (int i = 0; i < 64; i++)
                printf(" %02x", (unsigned char)map[i]);
        putchar('\n');

        munmap(map, BUFSIZE);
        close(fd);
        return (0);
}
```

Run it after writing some bytes into the device:

```sh
$ printf 'ABCDEFGHIJKL' > /dev/myfirst
$ ./mmap_myfirst
first 64 bytes:
 41 42 43 44 45 46 47 48 49 4a 4b 4c 00 00 00 00 ...
```

The first twelve bytes are `A`, `B`, ..., `L`, exactly what was written. The remaining bytes are zero because `cbuf_init` zero-fills the backing memory and we have not written anything past offset 12. This is the basic mechanism.

#### When You Would Actually Use `d_mmap`

Most pseudo-devices do not need `d_mmap`. The syscall path is fast, simple, and well-understood, and the cost of an extra `read(2)` per page is negligible for low-rate data. Use `d_mmap` when one of the following applies:

- The data is being produced into the buffer at very high rates (gigabytes per second in graphics or high-end I/O) and the syscall overhead per byte starts to dominate.
- The user space wants to peek at or process specific positions in a large buffer without copying the whole thing.
- The driver represents hardware whose registers or DMA areas are addressable as memory (for instance, a GPU's command FIFO).

For our pseudo-device, `d_mmap` is mostly a learning exercise. Building it teaches you the call signature, the relationship to the VM system, and the `vtophys`/`contigmalloc` distinction. Real production use comes when you write a driver that demands the throughput.

### Topic 2: Zero-Copy Considerations

"Zero-copy" is one of the most overused words in systems performance discussions. Strictly read, it means "no data is copied between memory locations during the operation." That definition is too strict to be useful: even DMA from a device to memory is, technically, a copy. In practice, "zero-copy" is shorthand for "the bytes do not pass through the CPU's caches as part of an explicit copy instruction in the I/O path."

For a character device like `myfirst`, the question is whether you can avoid the `uiomove(9)` copy in the read and write handlers. The answer, for the patterns we have built, is "no, and trying to is usually a mistake." Here is why.

`uiomove(9)` does one copy from kernel to user (or user to kernel) for each transfer. That is one set of byte motions per `read(2)` or `write(2)` call. The CPU pulls the source into cache, writes the destination from cache, and goes on with its work. On modern hardware, this copy is fast: an L1 cache line is 64 bytes, the CPU can stream tens of gigabytes per second of memory copies, and the cost per byte is in the single-digit nanoseconds.

To eliminate that copy, you have to find another way to make the bytes visible to user space. The two main mechanisms are `mmap(2)` (which we just discussed) and shared-memory primitives (`shm_open(3)`, sockets with `MSG_PEEK`, sendfile). All of them have their own costs: page-table updates, TLB flushes, IPI traffic on multi-CPU systems, the inability to use the source memory for anything else while it is mapped. For small to medium transfers, `uiomove` is *faster* than the alternatives because the alternatives' setup costs dominate.

There are real cases where zero-copy pays off. A network driver that DMAs incoming packets into mbufs and hands the mbufs to the protocol stack avoids a copy that would otherwise cost as much as the DMA itself. A storage driver that uses `bus_dmamap_load(9)` to set up a DMA transfer from a user-space buffer (after `vslock`-ing it) avoids two copies that would otherwise dominate the I/O cost. A high-throughput graphics driver may map GPU command buffers directly into the rendering process to avoid a per-frame copy. All of these are real wins.

For a pseudo-device whose data does not come from real hardware, however, the gain is illusory. The "saved" copy is just rearranging where the bytes are stored; the cost shows up somewhere else (the page-table update, the cache miss when the user touches a page that has been written-through directly from the kernel, the contention when two CPUs touch the same shared page). The Stage 4 driver does a `uiomove` per bounce-buffer-sized chunk; that is roughly one copy per 256 bytes, which is well within the throughput a single core can sustain.

If you find yourself optimizing the copy out of a pseudo-device, two questions are worth asking first.

The first question is whether the copy is actually the bottleneck. Run the driver under `dtrace` or `pmcstat` and measure where the cycles are going. If `uiomove` is not in the top three, optimizing it will make no measurable difference. The most common bottlenecks in this kind of code are lock contention (one CPU is waiting for another to release the mutex), syscall overhead (many small syscalls instead of fewer large ones), and the cost of waking sleepers (every `wakeup` is a sleep-queue traversal). All of those offer bigger gains than the copy itself.

The second question is whether the *user* of the driver actually wants zero-copy semantics. A user that calls `read(2)` is asking the kernel to give them a copy of the bytes. They are not asking for a pointer to the kernel's bytes. Switching to a mapping changes the contract; the user has to know about the mapping, manage it explicitly, and understand the cache-coherency rules. That is a trade-off the user has to opt into, not a transparent improvement.

The right framing is: zero-copy is a technique with specific costs and specific benefits. Use it when the benefits clearly outweigh the costs, and not before. For most drivers, especially pseudo-devices, the syscall path with `uiomove` is the right choice.

### Topic 3: Readahead and Write Coalescing

The third topic is about throughput. When a driver supports a steady high-rate stream of bytes, two patterns become important: **readahead** on the read side and **write coalescing** on the write side. Both are about doing more work per syscall, and both reduce the per-byte overhead of the I/O path.

#### Readahead

Readahead is the act of fetching more data than the user has currently asked for, on the assumption that they will ask for it next. A regular-file read often triggers readahead at the VFS level: when the kernel notices that a process has read a few sequential blocks, it starts reading the next blocks in the background so that the next `read(2)` finds them already in memory. The user sees lower latency on subsequent reads.

For a pseudo-device, readahead at the VFS level is not directly applicable (there is no underlying file). However, the *driver* can do its own form of readahead by asking the data source to produce ahead of time. Imagine a driver that wraps a slow data source (a hardware sensor, a remote service). When the user reads, the driver pulls data from the source. The user reads again; the driver pulls more. With readahead, the driver might pull a *block* of data from the source the first time the user reads, store the extra bytes in the cbuf, and serve subsequent reads directly from the cbuf without going back to the source.

This is exactly what the `myfirst` driver already does in spirit. The cbuf *is* the readahead buffer. Writes deposit data, reads consume it, and the reader does not have to wait for the writer to write each individual byte. The wider lesson is that having a buffer in the driver is, structurally, the same pattern as readahead: it lets the consumer find data already prepared.

When you do build a driver against a real source, the readahead logic typically lives in a kernel thread or callout that watches `cbuf_used` and triggers a fetch from the source when the count drops below a threshold. The threshold is the *low-water mark*; the fetch stops when the count reaches the *high-water mark*. The cbuf becomes a buffer between the source's burst rate and the consumer's burst rate, and the kernel thread keeps it appropriately full.

#### Write Coalescing

Write coalescing is the mirror pattern. A driver that sinks data into a slow destination (a hardware register, a remote service) might collect several small writes into a single large write, reducing the per-write overhead at the destination. The user's `write(2)` calls deposit bytes into the cbuf; a kernel thread or callout reads from the cbuf and writes to the destination in larger chunks.

Coalescing is especially useful when the destination has high per-operation overhead. Consider a driver that talks to a chip whose command structure expects a header, payload, and footer per write: a single 1024-byte write to the chip might be twenty times faster than a thousand 1-byte writes, because the per-write overhead dominates at small sizes. The driver coalesces by collecting bytes in the cbuf and flushing them in larger chunks.

The decision of *when* to flush is the hard part. Two common policies exist: **flush at threshold** (flush when `cbuf_used` exceeds a high-water mark) and **flush at timeout** (flush after a fixed delay since the first byte arrived). Most real drivers use a combination: flush whenever either condition is met. A `callout(9)` (the kernel's deferred-execution primitive) is the natural way to schedule the timeout. Chapter 13 covers `callout` in detail; for now, the conceptual point is that coalescing is a deliberate trade between per-byte latency (worse, because the byte sits in the buffer) and per-operation throughput (better, because the destination sees fewer larger writes).

#### How These Patterns Apply to `myfirst`

The `myfirst` driver does not need either pattern explicitly because it has no real source or sink. The cbuf already provides the coupling between the writer and the reader, and the only "flush" is the natural one that happens when the reader calls `read(2)`. But knowing the patterns is useful for two reasons.

First, when you read driver code in `/usr/src/sys/dev/`, you will see these patterns repeatedly. Network drivers coalesce TX writes through queues. Audio drivers do readahead by fetching DMA blocks ahead of the consumer. Block-device drivers use the BIO layer to coalesce I/O requests by sector adjacency. Recognising the pattern lets you skim a thousand lines of driver code without losing the plot.

Second, when you start writing real-hardware drivers in Part 4 and beyond, you will need to decide whether and how to apply these patterns to your driver. The Chapter 10 work has given you the *substrate* (a circular buffer with proper locking and blocking semantics). Adding readahead means starting a kernel thread to fill it. Adding coalescing means flushing it on a timer or threshold. The substrate is the same; the policies are different.

### Wrapping Up Section 8

These three topics (`d_mmap`, zero-copy, readahead/coalescing) are common follow-on conversations in driver development. None of them is a Chapter 10 topic in its own right, but each one builds on the buffer abstraction and the I/O machinery you have just put in place.

`d_mmap` adds a complementary path to the buffer: in addition to `read(2)` and `write(2)`, user space can now look at the bytes directly. Zero-copy is the framing that explains why `d_mmap` matters in some cases and is overkill in others. Readahead and write coalescing are the patterns that turn a buffered driver into a high-throughput driver.

The next sections of the chapter return to your current driver: hands-on labs that consolidate the four stages, challenge exercises that stretch your understanding, and a troubleshooting section for the bugs this material is most likely to produce.

## Hands-On Labs

The labs below walk you through the four stages of the chapter, with concrete checkpoints between them. Each lab corresponds to a milestone you can verify with the test kit from Section 6. They are designed to be done in order; later labs assume earlier ones are complete.

A general note: at the start of every lab session, do a `kldunload myfirst` (if the previous module is still loaded) and a fresh `kldload ./myfirst.ko`. Watch `dmesg | tail` for the attach message. If the attach fails, the rest of the lab will fail in confusing ways; fix the attach first.

### Lab 1: The Standalone Circular Buffer

**Goal:** Build and verify the userland `cbuf` implementation. This lab is entirely in user space; no kernel module is involved.

**Steps:**

1. Create the directory `examples/part-02/ch10-handling-io-efficiently/cbuf-userland/` if it does not already exist.
2. Type `cbuf.h` and `cbuf.c` exactly as shown in Section 2. Resist the temptation to skim the source from the book; typing it forces you to notice every line.
3. Type `cb_test.c` from Section 2.
4. Build with `cc -Wall -Wextra -o cb_test cbuf.c cb_test.c`.
5. Run `./cb_test`. You should see three "OK" lines and the final "all tests OK".

**Checkpoint questions:**

- What does `cbuf_write(&cb, src, n)` return when the buffer is already full?
- What does `cbuf_read(&cb, dst, n)` return when the buffer is already empty?
- After `cbuf_init(&cb, 4)` and `cbuf_write(&cb, "ABCDE", 5)`, what is `cbuf_used(&cb)`? What is the content of `cb.cb_data` (positions 0..3)?

If you cannot answer these from your own code, re-read Section 2 and trace through the source.

**Stretch goal:** add a fourth test, `test_alternation`, that writes one byte, reads it back, writes another byte, reads it back, and so on for 100 iterations. This catches off-by-one errors in `cbuf_read` that the existing tests do not.

### Lab 2: The Stage 2 Driver (Circular Buffer Integration)

**Goal:** Move the verified `cbuf` into the kernel and replace the Stage 3 linear FIFO from Chapter 9.

**Steps:**

1. Create `examples/part-02/ch10-handling-io-efficiently/stage2-circular/`.
2. Copy `cbuf.h` from your userland directory into the new directory.
3. Type the kernel-side `cbuf.c` from Section 3 (this is the `MALLOC_DEFINE`-using version).
4. Copy `myfirst.c` from `examples/part-02/ch09-reading-and-writing/stage3-echo/` into the new directory.
5. Modify `myfirst.c` to use the cbuf abstraction. The changes are:
   - Add `#include "cbuf.h"` near the top.
   - Replace `char *buf; size_t buflen, bufhead, bufused;` with `struct cbuf cb;` in the softc.
   - Update `myfirst_attach` to call `cbuf_init(&sc->cb, MYFIRST_BUFSIZE)`. Update the failure path to call `cbuf_destroy`.
   - Update `myfirst_detach` to call `cbuf_destroy(&sc->cb)`.
   - Replace `myfirst_read` and `myfirst_write` with the loop-and-bounce versions from Section 3.
   - Update the sysctl handlers as in Section 3 (use the `myfirst_sysctl_cb_used` and `myfirst_sysctl_cb_free` helpers).
6. Update the `Makefile` to build both source files: `SRCS= myfirst.c cbuf.c device_if.h bus_if.h`.
7. Build with `make`. Fix any compilation errors.
8. Load with `kldload ./myfirst.ko` and verify with `dmesg | tail`.

**Verification:**

```sh
$ printf 'helloworld' > /dev/myfirst
$ sysctl dev.myfirst.0.stats.cb_used
dev.myfirst.0.stats.cb_used: 10
$ cat /dev/myfirst
helloworld
$ sysctl dev.myfirst.0.stats.cb_used
dev.myfirst.0.stats.cb_used: 0
```

**Stretch goal 1:** write enough bytes to wrap the buffer (write 3000 bytes, read 2000, write 2000 again). Verify that `cb_head` is non-zero in `sysctl` and that the data still comes back correctly.

**Stretch goal 2:** add a sysctl-controlled debug flag (`myfirst_debug`) and a `MYFIRST_DBG` macro (Section 3 shows the pattern). Use it to log every successful `cbuf_read` and `cbuf_write` in the I/O handlers. Set the flag with `sysctl dev.myfirst.debug=1` and watch `dmesg`.

### Lab 3: The Stage 3 Driver (Blocking and Non-Blocking)

**Goal:** Add blocking-on-empty, blocking-on-full, and `EAGAIN` for non-blocking callers.

**Steps:**

1. Create `examples/part-02/ch10-handling-io-efficiently/stage3-blocking/` and copy your Stage 2 source into it.
2. Modify `myfirst_read` to add the inner sleep loop (Section 5). The new shape includes the `nbefore = uio->uio_resid` snapshot, the `mtx_sleep` call, and the `wakeup(&sc->cb)` after a successful read.
3. Modify `myfirst_write` to add the symmetric sleep loop and the matching `wakeup(&sc->cb)`.
4. Update `myfirst_detach` to set `sc->is_attached = 0` *before* calling `wakeup(&sc->cb)`, all under the mutex.
5. Build, load, and verify.

**Verification:**

```sh
$ cat /dev/myfirst &
[1] 12345
$ ps -AxH -o pid,wchan,command | grep cat
12345 myfrd  cat /dev/myfirst
$ echo hi > /dev/myfirst
hi
[after the cat consumes "hi", it blocks again]
$ kill -INT %1
[1]    Interrupt: 2
```

**Verification of `EAGAIN`:**

```sh
$ ./rw_myfirst_nb       # from the userland directory
step 1: empty-read returned EAGAIN (expected)
step 2: poll(POLLIN, 0) = 0 revents=0x0
...
```

If step 1 still says `read returned 0`, your `IO_NDELAY` check in `myfirst_read` is missing or wrong.

**Stretch goal 1:** open two `cat` processes against `/dev/myfirst` simultaneously. Write 100 bytes from a third terminal. Both `cat`s should wake up; one will get the bytes (whichever wins the race for the lock), the other will block again. You can verify the assignment by tagging each `cat` with a different output stream: `cat /dev/myfirst > /tmp/a &` and `cat /dev/myfirst > /tmp/b &`, then `cmp /tmp/a /tmp/b` (one will be empty).

**Stretch goal 2:** time how long `cat /dev/myfirst` takes to wake up after a write, using `time(1)`. The wake-up latency should be in the low microseconds; if it is in milliseconds, something is buffering between the write and the wake (or your machine is heavily loaded).

### Lab 4: The Stage 4 Driver (Poll Support and Refactor)

**Goal:** Add `d_poll`, refactor buffer access into helpers, and document the locking strategy.

**Steps:**

1. Create `examples/part-02/ch10-handling-io-efficiently/stage4-poll-refactor/` and copy your Stage 3 source.
2. Add `struct selinfo rsel; struct selinfo wsel;` to the softc.
3. Implement `myfirst_poll` as in Section 5.
4. Add `selwakeup(&sc->wsel)` after the read's successful `cbuf_read`, and `selwakeup(&sc->rsel)` after the write's successful `cbuf_write`.
5. Add `seldrain(&sc->rsel); seldrain(&sc->wsel);` to detach.
6. Add `.d_poll = myfirst_poll,` to the `cdevsw`.
7. Refactor the I/O handlers to use the four helpers from Section 7 (`myfirst_buf_read`, `myfirst_buf_write`, `myfirst_wait_data`, `myfirst_wait_room`).
8. Add the locking-strategy comment from Section 7.
9. Build, load, and verify.

**Verification:**

```sh
$ ./rw_myfirst_nb
step 1: empty-read returned EAGAIN (expected)
step 2: poll(POLLIN, 0) = 0 revents=0x0
step 3: wrote 12 bytes
step 4: poll(POLLIN, 0) = 1 revents=0x41
step 5: read 12 bytes: hello world
```

The key change from Lab 3 is that step 4 should now return `1` (not `0`), with `revents=0x41` (POLLIN | POLLRDNORM). If it still returns 0, your `selwakeup` call is missing from the write path or the `myfirst_poll` handler is wrong.

**Stretch goal 1:** run `producer_consumer` with `TOTAL_BYTES = 8 * 1024 * 1024` (8 MB) and verify that the test completes with no mismatches. The producer is generating bytes faster than the consumer is reading them, so the buffer should fill up and trigger the blocking path repeatedly. Watch `sysctl dev.myfirst.0.stats.cb_used` in another terminal; it should oscillate.

**Stretch goal 2:** run two `producer_consumer`s in parallel against the same device. The two writers will compete for buffer space; the two readers will compete for bytes. Each pair should still see consistent checksums, but the *interleaving* of bytes will be unpredictable. This shows that the driver is single-stream per device, not per descriptor; if you need per-descriptor streams, that is a different driver design.

### Lab 5: Memory Mapping

**Goal:** Add `d_mmap` so user space can map the cbuf read-only.

**Steps:**

1. Create `examples/part-02/ch10-handling-io-efficiently/stage5-mmap/` and copy your Stage 4 source.
2. Add `myfirst_mmap` from Section 8 to the source.
3. Add `.d_mmap = myfirst_mmap,` to the `cdevsw`.
4. Build, load, and verify.

**Verification:**

```sh
$ printf 'ABCDEFGHIJKL' > /dev/myfirst
$ ./mmap_myfirst       # from the userland directory
first 64 bytes:
 41 42 43 44 45 46 47 48 49 4a 4b 4c 00 00 00 ...
```

The first twelve bytes are the bytes you wrote.

**Stretch goal 1:** write a small program that maps the buffer and reads bytes from `offset = sc->cb_size - 32` (i.e. the last 32 bytes). Verify that the program does not crash. Then write enough bytes to push the buffer head into the wrap region and read from the same offset. The contents will be different, because the *raw* bytes in memory are not the same as the *live* bytes from the cbuf's perspective.

**Stretch goal 2:** try to map the buffer with `PROT_WRITE`. Your program should see `mmap` fail with `EACCES`, because the driver refuses writable mappings.

### Lab 6: Stress and Long-Running Tests

**Goal:** Run the driver under sustained load for at least an hour without errors.

**Steps:**

1. Set up four parallel test processes:
   - `dd if=/dev/zero of=/dev/myfirst bs=4k 2>/dev/null &`
   - `dd if=/dev/myfirst of=/dev/null bs=4k 2>/dev/null &`
   - `./producer_consumer`
   - A loop that polls `sysctl dev.myfirst.0.stats` every 5 seconds.
2. Let the test run for at least an hour.
3. Check `dmesg` for any kernel warnings, panics, or `WITNESS` complaints. Check `vmstat -m | grep cbuf` to confirm no leak. Verify that `producer_consumer` reports zero mismatches.

**Verification:** No kernel warnings. No memory growth in `vmstat`. `producer_consumer` returns 0.

**Stretch goal:** run the same test under a `WITNESS`-enabled debug kernel. The kernel will be slower but will catch any locking-discipline violations. If your driver is correct, no warnings should appear.

### Lab 7: Deliberate Failures

**Goal:** Break the driver in three specific ways and observe what happens. This lab teaches you to recognise the failure modes you most want to avoid.

**Steps for failure 1: hold the lock across `uiomove`.**

1. Edit your Stage 4 driver. In `myfirst_read`, comment out the `mtx_unlock(&sc->mtx)` that comes before `uiomove(bounce, got, uio)`.
2. Add a matching `mtx_unlock` after the `uiomove` so the code still compiles.
3. Build and load on a `WITNESS`-enabled kernel.
4. Run a single `cat /dev/myfirst` and write some bytes from another terminal.

**What you should observe:** A `WITNESS` warning in `dmesg` complaining about "sleeping with mutex held". The system may continue running but the warning is the bug.

**Cleanup:** restore the original code.

**Steps for failure 2: forget the `wakeup` after a write.**

1. In `myfirst_write`, comment out `wakeup(&sc->cb)`.
2. Build and load.
3. Run `cat /dev/myfirst &` and `echo hi > /dev/myfirst`.

**What you should observe:** The `cat` does not wake up. It will sit in `myfrd` state forever (or until you interrupt it with Ctrl-C).

**Cleanup:** restore the wakeup. Verify that `cat` now wakes up immediately.

**Steps for failure 3: missing `PCATCH`.**

1. In `myfirst_wait_data`, change `PCATCH` to `0` in the `mtx_sleep` call.
2. Build and load.
3. Run `cat /dev/myfirst &` and try `kill -INT %1`.

**What you should observe:** The `cat` does not respond to Ctrl-C until you write some bytes to wake it. With `PCATCH`, the signal would interrupt the sleep immediately.

**Cleanup:** restore `PCATCH`. Verify that `kill -INT` works as expected.

These three failures are the most common driver bugs in this chapter's territory. Doing them deliberately, once, is the best way to recognise them when they happen accidentally.

### Lab 8: Reading Real FreeBSD Drivers

**Goal:** Read three character-device drivers in `/usr/src/sys/dev/` and identify how each implements its buffer, sleep, and poll patterns.

**Steps:**

1. Read `/usr/src/sys/dev/evdev/cdev.c`. Identify:
   - Where the per-client ring buffer is allocated.
   - Where the read handler blocks (look for `mtx_sleep`).
   - How `EVDEV_CLIENT_EMPTYQ` is implemented.
   - How `kqueue` is set up alongside `select/poll` (we have not done `kqueue` yet; just notice the calls to `knlist_*`).
2. Read `/usr/src/sys/dev/random/randomdev.c`. Identify:
   - Where `randomdev_poll` is defined.
   - How it handles a not-yet-seeded random device.
3. Read `/usr/src/sys/dev/null/null.c`. Identify:
   - How `zero_read` loops over `uio_resid`.
   - Why there is no buffer, no sleep, and no poll handler.

**Checkpoint questions:**

- Why does `evdev`'s read handler use `mtx_sleep` while `null`'s does not?
- What would `randomdev`'s poll handler return if called while the device is unseeded?
- How does `evdev` detect that a client has been disconnected (revoked)?

The point of this lab is not to memorise these drivers. It is to confirm that the patterns you have built in `myfirst` are the same patterns the kernel uses elsewhere. By the end of the lab you should feel that the rest of `dev/` is largely *legible* now, where it might have looked impenetrable two chapters ago.

## Challenge Exercises

The labs above ensure you have a working driver and a working test kit. The challenges below are stretch exercises. Each one extends the chapter's material in a useful direction, and each one rewards careful work. Take your time; some of them are more involved than they look.

### Challenge 1: Add a Tunable Buffer Size

The `MYFIRST_BUFSIZE` macro hard-codes the buffer at 4 KB. Make it configurable.

- Add a `TUNABLE_INT("hw.myfirst.bufsize", &myfirst_bufsize)` and a matching `SYSCTL_INT(_hw_myfirst, OID_AUTO, bufsize, ...)` so the user can set the buffer size at module load time.
- Use the value in `myfirst_attach` to size the cbuf.
- Validate the value (reject zero, reject sizes larger than 1 MB, fall back to a sensible default if the input is bad).
- Verify with `kenv hw.myfirst.bufsize=8192; kldload ./myfirst.ko; sysctl dev.myfirst.0.stats.cb_size`.

**Stretch:** make the buffer size *runtime-tunable* via `sysctl`. This is harder than the load-time tunable because it requires safely reallocating the cbuf while the device may be in use; you will need to drain or copy existing bytes, take and release the lock at the right moments, and decide what to do with sleeping callers. (Hint: it may be easier to require that all descriptors be closed before allowing a runtime resize.)

### Challenge 2: Implement Overwrite Semantics as an Optional Mode

Add an `ioctl(2)` (or, simpler for now, a `sysctl`) that switches the buffer between "block on full" mode (the default) and "overwrite oldest on full" mode. In overwrite mode, `myfirst_write` always succeeds: when `cbuf_free` is zero, the driver advances `cb_head` to make room and then writes the new bytes.

- Add a `cbuf_overwrite` function alongside `cbuf_write` that implements the overwrite semantics. Do not modify `cbuf_write`; the two should be siblings.
- Add a sysctl `dev.myfirst.0.overwrite_mode` (read-write integer, 0 or 1).
- In `myfirst_write`, dispatch to `cbuf_overwrite` if the flag is set.
- Test with a small writer that produces bytes faster than the reader consumes; in overwrite mode, the reader should see the most recent bytes only, while in normal mode the writer blocks.

**Stretch:** add a counter for the number of bytes overwritten (lost). Expose it as a sysctl so the user can see how much data has been dropped.

### Challenge 3: Per-Reader Position

The current driver has one shared read position (`cb_head`). When two readers consume bytes, each `read(2)` call drains some bytes from the buffer; the two readers split the stream between them. Some drivers want the opposite: each reader should see *every* byte, so two readers each get the full stream independently.

This is a substantial refactor:

- Maintain a per-descriptor read position in `myfirst_fh`.
- Track the global "earliest live byte" across all descriptors. The cbuf's effective `head` becomes `min(per_fh_head)`.
- `myfirst_read` advances only the per-descriptor position; `cbuf_read` is replaced by a per-fh equivalent.
- A new descriptor opened mid-stream sees only bytes written after its open.
- When the buffer is "full" depends on the slowest descriptor; you need backpressure logic that accounts for laggards.

This challenge is harder than it sounds; it is essentially building a multicast pipe. Try it only if you have time to think through the locking carefully.

### Challenge 4: Implement `d_kqfilter`

Add `kqueue(2)` support alongside the `d_poll` you already have.

- Implement a `myfirst_kqfilter` function dispatched from `cdevsw->d_kqfilter`.
- For `EVFILT_READ`, register a filter that becomes ready when `cbuf_used > 0`.
- For `EVFILT_WRITE`, register a filter that becomes ready when `cbuf_free > 0`.
- Use `knlist_add(9)` and `knlist_remove(9)` to manage the per-filter list.
- Trigger `KNOTE_LOCKED(...)` from the I/O handlers when the buffer transitions.
- Test with a small `kqueue(2)` user program that opens the device, registers `EVFILT_READ`, calls `kevent(2)`, and reports when the descriptor becomes readable.

This challenge is the natural extension of Stage 4. It also previews the `kqueue` material that Chapter 11 will discuss in more depth alongside concurrency.

### Challenge 5: Per-CPU Counters

The `bytes_read` and `bytes_written` counters are updated under the mutex. Under heavy multi-CPU load, this can become a contention point. FreeBSD's `counter(9)` API provides per-CPU counters that can be incremented without a lock and summed for read access.

- Replace `sc->bytes_read` and `sc->bytes_written` with `counter_u64_t` instances.
- Allocate them with `counter_u64_alloc(M_WAITOK)` in attach; free them with `counter_u64_free` in detach.
- Use `counter_u64_add(counter, n)` to increment.
- Use `counter_u64_fetch(counter)` (with a sysctl handler) to read.

**Stretch:** measure the difference. Run `producer_consumer` against the old and new versions and compare wall-clock time. With a small test the difference will be invisible; with a heavily threaded test (multiple producers and consumers) the per-CPU version should be measurably faster.

### Challenge 6: A Hardware-Style Interrupt Simulator

Real driver buffers are usually filled by an interrupt handler, not by a `write(2)` syscall. Simulate this:

- Use `callout(9)` (Chapter 13 covers it; you can read ahead) to run a callback every 100 ms.
- The callback writes a small piece of data into the cbuf (for example, the current time as a string).
- The user reads from `/dev/myfirst` and sees a stream of timestamped lines.

This challenge previews Chapter 13's deferred-execution material and shows how the same buffer abstraction supports either a syscall-driven producer or a kernel-thread-driven producer.

### Challenge 7: A Logging Buffer with `dmesg`-Style Behaviour

Build a second character device, `/dev/myfirst_log`, that uses an overwrite-mode cbuf to keep a circular log of recent driver events. Every `MYFIRST_DBG` macro call would write into this log instead of (or in addition to) calling `device_printf`.

- Use a separate `struct cbuf` in the softc.
- Provide a way for the kernel side to push lines into the log (`myfirst_log_printf(sc, fmt, ...)`).
- The user can `cat /dev/myfirst_log` to see the recent N lines.
- A new line that overflows the buffer evicts the oldest line, not just the oldest byte (this requires line-aware eviction logic).

This challenge introduces a fairly common driver pattern (a private debug log) and gives you practice with a second, independently designed buffer use case in the same module.

### Challenge 8: Performance Measurement

Build a measurement harness that times the throughput of the driver across the four stages.

- Write a small C program that opens the device, writes 100 MB of data, and times the operation.
- Mirror it with a reader that drains 100 MB and times itself.
- Run the pair against Stage 2, Stage 3, and Stage 4 of the chapter, and produce a small table of throughput numbers.
- Identify which stage is slowest and explain why.

The expected answer is "Stage 3 is slower than Stage 2 because of the extra `wakeup` and `selwakeup` calls per iteration; Stage 4 is similar to Stage 3 within measurement noise". But the actual numbers are interesting and may surprise you, depending on your CPU, memory bandwidth, and system load.

**Stretch:** profile the driver under load with `pmcstat(8)` and identify the top three functions by CPU time. If `uiomove` is in the top three, you have validated the discussion in Section 8 about zero-copy. If `mtx_lock` is in the top three, you have a contention problem that Chapter 11's locking material will address.

### Challenge 9: Cross-Reading Real Drivers

Pick three drivers in `/usr/src/sys/dev/` that you have not read before. For each one, identify:

- Where the buffer is allocated and freed.
- Whether it is a circular buffer, a queue, or a different shape.
- What protects it (mutex, sx, lock-free, none).
- How `read` and `write` handlers consume from or produce into it.
- How `select`/`poll`/`kqueue` integrate with the buffer state changes.

Suggested starting points: `/usr/src/sys/dev/iicbus/iiconf.c` (different category but uses some of the same primitives) and `/usr/src/sys/fs/cuse/cuse.c` (a driver that exposes its buffer to user space). You will see variations on the same themes you have just built.

### Challenge 10: Document Your Driver

Write a one-page README in your `examples/part-02/ch10-handling-io-efficiently/stage4-poll-refactor/` directory. The README should cover:

- What the driver does.
- How to build it (`make`).
- How to load and unload (`kldload`, `kldunload`).
- The user-space interface: device path, mode, reader/writer expectations, blocking behaviour.
- What the sysctls expose.
- How to enable debug logging.
- A reference to the chapter that produced it.

Documentation is the part of driver work most often skipped. A driver that only its author understands is a maintenance liability. Even a one-page README that explains the basics makes the difference between code that survives a hand-off and code that does not.

## Troubleshooting and Common Mistakes

Most of the bugs that appear in this chapter's territory cluster into a small number of categories. The list below catalogues the categories, the symptoms each one produces, and the fix. Read it once before you work through the labs; come back to it when something goes wrong.

### Symptom: `cat /dev/myfirst` blocks forever, even after `echo` writes data

**Cause.** The write handler is not calling `wakeup(&sc->cb)` after a successful `cbuf_write`. The reader is asleep on the channel `&sc->cb`; without a matching `wakeup`, it will never return.

**Fix.** Add `wakeup(&sc->cb)` after every state-changing operation that might unblock a waiter. In `myfirst_write`, that means after the `cbuf_write` call. In `myfirst_read`, that means after the `cbuf_read` call (which may unblock a waiting writer).

**How to verify.** Run `ps -AxH -o pid,wchan,command | grep cat`. If the `wchan` column shows `myfrd` (or whatever wmesg you used), the reader is sleeping. The channel address you slept on must match the channel address you wake.

### Symptom: Data corruption under heavy load

**Cause.** Almost always a wrap-around bug or a missing lock around the cbuf access. Either the cbuf's internal arithmetic is wrong, or two threads are touching it concurrently without synchronization.

**Fix.** Re-read the cbuf source carefully. Run the userland `cb_test` against your current `cbuf.c` (compile it directly with `cc`). If the userland tests pass, the problem is in the driver's locking, not in the cbuf. Check that every `cbuf_*` call is bracketed by `mtx_lock` and `mtx_unlock`. Use `INVARIANTS` and `WITNESS` in your kernel config to catch violations.

**How to verify.** Run `producer_consumer` with a known checksum. If the checksums match but mismatches are reported, the data is being reordered (a wrap-around bug). If the checksums differ, bytes are being lost or duplicated (a locking bug).

### Symptom: Kernel panic with "sleeping with mutex held"

**Cause.** You called `uiomove(9)`, `copyin(9)`, `copyout(9)`, or another sleeping function while holding `sc->mtx`. The sleeping function tried to fault on user memory, and the page-fault handler tried to sleep, but holding a non-sleepable mutex during a sleep is forbidden.

**Fix.** Release the mutex before any call that might sleep. The Stage 4 handlers do this carefully: lock to access the cbuf, unlock to call `uiomove`, lock again to update state.

**How to verify.** A `WITNESS`-enabled kernel will print a warning before panicking. The warning identifies the mutex and the sleeping function. The first time this happens, copy the message into a debug log so you can find the call site.

### Symptom: `EAGAIN` is returned even when data is available

**Cause.** The handler is checking the wrong flag, or it is checking the flag in the wrong place in the loop. Two common variants: checking `ioflag & O_RDONLY` instead of `ioflag & IO_NDELAY`, or returning `EAGAIN` after some bytes have already been transferred (which is the rule from Section 4 you must not break).

**Fix.** Re-read Section 5's handler code carefully. The `EAGAIN` path is inside the inner `while (cbuf_used(&sc->cb) == 0)` loop, after the `nbefore` check, only when `ioflag & IO_NDELAY` is non-zero.

**How to verify.** Run `rw_myfirst_nb`. Step 5 should successfully read the bytes. If it shows `EAGAIN`, the bug is at one of the two locations above.

### Symptom: A write succeeds but a subsequent read gets fewer bytes

**Cause.** The byte counter is being updated incorrectly, or the cbuf is being modified outside the handlers. A specific failure mode: counting `want` bytes as written when `cbuf_write` only stored `put` bytes (a race in Stage 2 between the cbuf_free check and the cbuf_write call, even though it is not exercised in single-writer use).

**Fix.** Look at the `bytes_written += put` line in `myfirst_write`; it must use the actual return value of `cbuf_write`, not the requested size. Compare `sc->bytes_written` and `sc->bytes_read` over time; they should differ by at most `cbuf_size`.

**How to verify.** Add a log line: `device_printf(dev, "wrote %zu of %zu\n", put, want);`. If `put != want` ever appears in `dmesg`, you have found the discrepancy.

### Symptom: `kldunload` returns `EBUSY`

**Cause.** Some descriptor is still open against the device. The detach refuses to proceed when `active_fhs > 0`.

**Fix.** Find the process that holds the descriptor open and close it. `fstat | grep myfirst` lists the offending processes. `kill` them if necessary.

**How to verify.** After closing all descriptors (or killing the offending processes), `sysctl dev.myfirst.0.stats.active_fhs` should drop to zero. `kldunload myfirst` should then succeed.

### Symptom: Memory growth in `vmstat -m | grep cbuf`

**Cause.** The driver is allocating without freeing. Either the attach failure path forgot to call `cbuf_destroy`, or the detach path forgot, or there is more than one cbuf being allocated per attach.

**Fix.** Audit every code path that calls `cbuf_init`. Each call must be matched by exactly one `cbuf_destroy` call before the surrounding context goes away. The standard idiom is to put `cbuf_init` near the top of `attach` and `cbuf_destroy` near the bottom of `detach`, with the failure-path `goto fail_*` chain calling `cbuf_destroy` if attach fails after `cbuf_init`.

**How to verify.** `kldload` and `kldunload` the module several times. `vmstat -m | grep cbuf` should show `0` after each `kldunload`.

### Symptom: `select(2)` or `poll(2)` does not wake up

**Cause.** The driver is missing a `selwakeup` call when state changes. Either the read path forgot to call `selwakeup(&sc->wsel)` after draining bytes, or the write path forgot to call `selwakeup(&sc->rsel)` after adding bytes.

**Fix.** The pattern: every state change that might make a previously-not-ready condition into a ready condition must be paired with a `selwakeup` call. Drain bytes -> `selwakeup(&sc->wsel)`. Add bytes -> `selwakeup(&sc->rsel)`.

**How to verify.** Run `rw_myfirst_nb`. Step 4 should show `revents=0x41`. If it shows `revents=0x0`, your `selwakeup` is missing or the `myfirst_poll` handler is not setting `revents` correctly.

### Symptom: `truss` shows `EINVAL` for zero-byte reads

**Cause.** Your handler is rejecting zero-byte reads with `EINVAL`. As discussed in Section 4, zero-byte reads and writes are legal and the handler must not error on them.

**Fix.** Remove any `if (uio->uio_resid == 0) return (EINVAL);` early return at the top of `myfirst_read` or `myfirst_write`.

**How to verify.** A program that calls `read(fd, NULL, 0)` should see the call return `0`, not `-1` with `EINVAL`.

### Symptom: `ps` shows the reader stuck in a sleep state called something different

**Cause.** Your `mtx_sleep` is being called with a different `wmesg` than you expected. Two common variants: typo (`mfyrd` instead of `myfrd`), or the same handler is being called from a code path where the wait reason is actually different.

**Fix.** Standardise the `wmesg` strings. `myfrd` for "myfirst read", `myfwr` for "myfirst write". A unique short string per wait point makes `ps -AxH` immediately informative.

**How to verify.** `ps -AxH` should show `myfrd` for sleeping readers and `myfwr` for sleeping writers.

### Symptom: A signal does not interrupt a blocked read

**Cause.** The `mtx_sleep` is being called without `PCATCH`. Without `PCATCH`, signals are deferred until the sleep ends for some other reason.

**Fix.** Always pass `PCATCH` for user-driven sleeps. The exception is sleeps that should be uninterruptible (kernel internal logic that must not be cancelled by a signal). For `myfirst_read` and `myfirst_write`, both are user-driven and both should pass `PCATCH`.

**How to verify.** `cat /dev/myfirst &` followed by `kill -INT %1` should cause `cat` to exit. If `cat` does not exit until you also write to the device (or send `kill -9`), `PCATCH` is missing.

### Symptom: Compiler warns about a missing prototype for `cbuf_read`

**Cause.** The driver source uses `cbuf_read` but does not include `cbuf.h`.

**Fix.** Add `#include "cbuf.h"` near the top of `myfirst.c`. The file path is relative to the source directory, so as long as both files are in the same directory the include will resolve.

**How to verify.** A clean build with no warnings.

### Symptom: `make` complains about missing `bus_if.h` or `device_if.h`

**Cause.** The Makefile is missing the standard `SRCS+= device_if.h bus_if.h` line that pulls in the auto-generated kobj headers for Newbus.

**Fix.** Use the Makefile from Section 3.

**How to verify.** `make clean && make` should succeed without missing-header errors.

### Symptom: `kldload` fails with `Exec format error`

**Cause.** The .ko was built against a different kernel than the one currently running. This typically happens when you reboot into a different kernel without rebuilding, or when you copy a .ko from one machine to another with different kernel sources.

**Fix.** `make clean && make` against the running kernel's `/usr/src`.

**How to verify.** `uname -a` should match the kernel version that built the .ko. Check `dmesg` after the failed `kldload` for more details.

### Symptom: The driver reports correct data but loses it after several runs

**Cause.** The cbuf is not being reset between attaches, or the softc is not being zero-initialised. With `M_ZERO` on the `malloc(9)` call (and the `cbuf_init` call zero-initializing its own state), this should not happen, but a partial fix that misses one of these can leave stale state.

**Fix.** Audit `myfirst_attach` to make sure every field of the softc is initialised explicitly. Use `M_ZERO` on the `malloc(9)` call that allocates the softc (Newbus does this automatically with `device_get_softc`, but verify). Use `cbuf_init` to set the cbuf's indices to zero.

**How to verify.** `kldload`, write some data, `kldunload`, `kldload` again. The new attach should report `cb_used = 0`.

### Symptom: `producer_consumer` reports a small number of mismatches under heavy load

**Cause.** A subtle locking bug, often related to the order of `wakeup` calls and the inner sleep loop's re-check. The classic symptom: under contention, occasionally a thread wakes up and consumes bytes that another thread had thought were still available.

**Fix.** Verify that every `mtx_sleep` is in a `while` (not `if`) loop, and that the loop re-checks the condition after waking. The wakeup is a *hint*, not a guarantee; a woken thread might find the condition false again because another thread got there first.

**How to verify.** `producer_consumer` should report zero mismatches across multiple runs. A mismatch count that varies between runs suggests a race; a mismatch count that is always exactly N suggests an off-by-one bug.

### General Advice for Debugging

Three habits make driver debugging much faster.

The first is to have a `WITNESS`-enabled kernel ready. `WITNESS` catches lock-ordering violations and "sleeping with mutex held" bugs that a production kernel would silently allow. The performance overhead is significant, so run `WITNESS` in your lab environment, not in production.

The second is to add `device_printf` log lines liberally during development, then remove them or guard them behind `myfirst_debug` before committing. The log buffer is finite, so do not log per-byte; one line per I/O call is the right granularity.

The third is to compile with `-Wall -Wextra` and treat warnings as bugs. The kernel build system passes a lot of warning flags by default; pay attention to them. Almost every warning is the kernel telling you about a real or potential bug.

When all else fails, sit back and trace through the code path on paper. A driver this size is small enough to fit on a single sheet. Ninety percent of the time, drawing the call graph and the lock acquisitions in order shows you the bug.

## Quick Reference: Patterns and Primitives

This reference is the chapter's material reduced to fast-lookup form. Use it after you have read the chapter; it is a reminder, not a tutorial.

### The Circular Buffer API

```c
struct cbuf {
        char    *cb_data;       /* backing storage */
        size_t   cb_size;       /* total capacity */
        size_t   cb_head;       /* next byte to read */
        size_t   cb_used;       /* live byte count */
};

int     cbuf_init(struct cbuf *cb, size_t size);
void    cbuf_destroy(struct cbuf *cb);
void    cbuf_reset(struct cbuf *cb);
size_t  cbuf_size(const struct cbuf *cb);
size_t  cbuf_used(const struct cbuf *cb);
size_t  cbuf_free(const struct cbuf *cb);
size_t  cbuf_write(struct cbuf *cb, const void *src, size_t n);
size_t  cbuf_read(struct cbuf *cb, void *dst, size_t n);
```

Rules:
- The cbuf does not lock; the caller is responsible.
- `cbuf_write` and `cbuf_read` clamp `n` to available space or live data and return the actual count.
- `cbuf_used` and `cbuf_free` return current state under the assumption that the caller holds whatever lock protects the cbuf.

### The Driver-Level Helpers

```c
size_t  myfirst_buf_read(struct myfirst_softc *sc, void *dst, size_t n);
size_t  myfirst_buf_write(struct myfirst_softc *sc, const void *src, size_t n);
int     myfirst_wait_data(struct myfirst_softc *sc, int ioflag, ssize_t nbefore,
            struct uio *uio);
int     myfirst_wait_room(struct myfirst_softc *sc, int ioflag, ssize_t nbefore,
            struct uio *uio);
```

Rules:
- All four helpers assert that `sc->mtx` is held with `mtx_assert(MA_OWNED)`.
- The wait helpers return `-1` to mean "break the outer loop, return 0 to user space".
- The wait helpers return `EAGAIN`, `EINTR`, `ERESTART`, or `ENXIO` for the corresponding conditions.

### The Read Handler Spine

```c
nbefore = uio->uio_resid;
while (uio->uio_resid > 0) {
        mtx_lock(&sc->mtx);
        error = myfirst_wait_data(sc, ioflag, nbefore, uio);
        if (error != 0) {
                mtx_unlock(&sc->mtx);
                return (error == -1 ? 0 : error);
        }
        take = MIN((size_t)uio->uio_resid, sizeof(bounce));
        got = myfirst_buf_read(sc, bounce, take);
        fh->reads += got;
        mtx_unlock(&sc->mtx);

        wakeup(&sc->cb);
        selwakeup(&sc->wsel);

        error = uiomove(bounce, got, uio);
        if (error != 0)
                return (error);
}
return (0);
```

### The Write Handler Spine

```c
nbefore = uio->uio_resid;
while (uio->uio_resid > 0) {
        mtx_lock(&sc->mtx);
        error = myfirst_wait_room(sc, ioflag, nbefore, uio);
        if (error != 0) {
                mtx_unlock(&sc->mtx);
                return (error == -1 ? 0 : error);
        }
        room = cbuf_free(&sc->cb);
        mtx_unlock(&sc->mtx);

        want = MIN((size_t)uio->uio_resid, sizeof(bounce));
        want = MIN(want, room);
        error = uiomove(bounce, want, uio);
        if (error != 0)
                return (error);

        mtx_lock(&sc->mtx);
        put = myfirst_buf_write(sc, bounce, want);
        fh->writes += put;
        mtx_unlock(&sc->mtx);

        wakeup(&sc->cb);
        selwakeup(&sc->rsel);
}
return (0);
```

### The Sleep Pattern

```c
mtx_lock(&sc->mtx);
while (CONDITION) {
        if (uio->uio_resid != nbefore)
                break_with_zero;
        if (ioflag & IO_NDELAY)
                return (EAGAIN);
        error = mtx_sleep(CHANNEL, &sc->mtx, PCATCH, "wmesg", 0);
        if (error != 0)
                return (error);
        if (!sc->is_attached)
                return (ENXIO);
}
/* condition is false now; act on the buffer */
```

Rules:
- Use `while`, not `if`, around the condition.
- Always pass `PCATCH` for user-driven sleeps.
- Always re-check the condition after `mtx_sleep` returns.
- Always check `is_attached` after waking, in case detach is pending.

### The Wake Pattern

```c
/* After a state change that might unblock a sleeper: */
wakeup(CHANNEL);
selwakeup(SELINFO);
```

Rules:
- The channel must match the channel passed to `mtx_sleep`.
- The selinfo must be the one that `selrecord` registered against.
- Use `wakeup` (wake all) for shared waiters; `wakeup_one` for single-handoff patterns.
- Spurious wakeups are safe; missing wakeups are bugs.

### The `d_poll` Handler

```c
static int
myfirst_poll(struct cdev *dev, int events, struct thread *td)
{
        struct myfirst_softc *sc = dev->si_drv1;
        int revents = 0;

        mtx_lock(&sc->mtx);
        if (events & (POLLIN | POLLRDNORM)) {
                if (cbuf_used(&sc->cb) > 0)
                        revents |= events & (POLLIN | POLLRDNORM);
                else
                        selrecord(td, &sc->rsel);
        }
        if (events & (POLLOUT | POLLWRNORM)) {
                if (cbuf_free(&sc->cb) > 0)
                        revents |= events & (POLLOUT | POLLWRNORM);
                else
                        selrecord(td, &sc->wsel);
        }
        mtx_unlock(&sc->mtx);
        return (revents);
}
```

### The `d_mmap` Handler

```c
static int
myfirst_mmap(struct cdev *dev, vm_ooffset_t offset, vm_paddr_t *paddr,
    int nprot, vm_memattr_t *memattr)
{
        struct myfirst_softc *sc = dev->si_drv1;

        if (sc == NULL || !sc->is_attached)
                return (ENXIO);
        if ((nprot & VM_PROT_WRITE) != 0)
                return (EACCES);
        if (offset >= sc->cb.cb_size)
                return (-1);
        *paddr = vtophys((char *)sc->cb.cb_data + (offset & ~PAGE_MASK));
        return (0);
}
```

### The `cdevsw`

```c
static struct cdevsw myfirst_cdevsw = {
        .d_version =    D_VERSION,
        .d_open =       myfirst_open,
        .d_close =      myfirst_close,
        .d_read =       myfirst_read,
        .d_write =      myfirst_write,
        .d_poll =       myfirst_poll,
        .d_mmap =       myfirst_mmap,
        .d_name =       "myfirst",
};
```

### Errno Values for I/O

| Errno     | Meaning                                          |
|-----------|--------------------------------------------------|
| `0`       | Success                                          |
| `EAGAIN`  | Would block; retry later                         |
| `EFAULT`  | Bad user pointer (from `uiomove`)                |
| `EINTR`   | Interrupted by a signal                          |
| `ENXIO`   | Device not present or torn down                  |
| `EIO`     | Hardware error                                   |
| `ENOSPC`  | Permanent out-of-space (block-on-full preferred) |
| `EACCES`  | Forbidden access mode                            |
| `EBUSY`   | Device is open or otherwise locked               |

### `ioflag` Bits

| Bit            | Source flag    | Meaning                                |
|----------------|----------------|----------------------------------------|
| `IO_NDELAY`    | `O_NONBLOCK`   | Caller is non-blocking                 |
| `IO_DIRECT`    | `O_DIRECT`     | Bypass caching where possible          |
| `IO_SYNC`      | `O_FSYNC`      | (write only) Synchronous semantics     |

`O_NONBLOCK == IO_NDELAY` per the kernel's CTASSERT.

### `poll(2)` Events

| Event        | Meaning                                          |
|--------------|--------------------------------------------------|
| `POLLIN`     | Readable: bytes are available                    |
| `POLLRDNORM` | Same as POLLIN for character devices             |
| `POLLOUT`    | Writable: space is available                     |
| `POLLWRNORM` | Same as POLLOUT for character devices            |
| `POLLERR`    | Error condition                                  |
| `POLLHUP`    | Hangup (peer closed)                             |
| `POLLNVAL`   | Invalid file descriptor                          |

A driver typically handles `POLLIN | POLLRDNORM` for read readiness and `POLLOUT | POLLWRNORM` for write readiness. The other events are usually set by the kernel, not the driver.

### Memory Allocator Reference

| Call                                        | When to use                          |
|---------------------------------------------|--------------------------------------|
| `malloc(n, M_DEVBUF, M_WAITOK \| M_ZERO)`   | Normal allocation, can sleep         |
| `malloc(n, M_DEVBUF, M_NOWAIT \| M_ZERO)`   | Cannot sleep (interrupt context)     |
| `free(p, M_DEVBUF)`                         | Free memory allocated above          |
| `MALLOC_DEFINE(M_TAG, "name", "desc")`      | Declare a private memory tag         |
| `contigmalloc(n, M_TAG, M_WAITOK, ...)`     | Physically contiguous allocation     |

### Sleep / Wake Reference

| Call                                                              | When to use                              |
|-------------------------------------------------------------------|------------------------------------------|
| `mtx_sleep(chan, mtx, PCATCH, "msg", 0)`                          | Sleep with mutex interlock               |
| `tsleep(chan, PCATCH \| pri, "msg", timo)`                        | Sleep without mutex (rare in drivers)    |
| `cv_wait(&cv, &mtx)`                                              | Sleep on a condition variable            |
| `wakeup(chan)`                                                    | Wake all sleepers on channel             |
| `wakeup_one(chan)`                                                | Wake one sleeper (for single-handoff)    |

### Lock Reference

| Call                                                | When to use                             |
|-----------------------------------------------------|-----------------------------------------|
| `mtx_init(&mtx, "name", "type", MTX_DEF)`           | Initialise a sleepable spin/sleep mutex |
| `mtx_destroy(&mtx)`                                 | Destroy at detach                       |
| `mtx_lock(&mtx)`, `mtx_unlock(&mtx)`                | Acquire / release                       |
| `mtx_assert(&mtx, MA_OWNED)`                        | Assert lock is held (debug)             |

### Test Tools Reference

| Tool                  | Use                                          |
|-----------------------|----------------------------------------------|
| `cat`, `echo`         | Quick smoke tests                            |
| `dd`                  | Volume tests, partial-transfer observation   |
| `hexdump -C`          | Verify byte content                          |
| `truss -t read,write` | Trace syscall returns                        |
| `ktrace`              | Detailed trace including signals             |
| `sysctl dev.myfirst.0.stats` | Live driver state                     |
| `vmstat -m`           | Memory tag accounting                        |
| `ps -AxH`             | Find sleeping threads and their wmesg        |
| `dmesg | tail`        | Driver-emitted log lines and kernel warnings |

### Driver Lifecycle Summary

```text
kldload
    -> myfirst_identify   (optional in this driver: creates the child)
    -> myfirst_probe      (returns BUS_PROBE_DEFAULT)
    -> myfirst_attach     (allocates softc, cbuf, cdev, sysctl, mutex)

steady state
    -> myfirst_open       (allocates per-fh state)
    -> myfirst_read       (drains cbuf via bounce + uiomove)
    -> myfirst_write      (fills cbuf via uiomove + bounce)
    -> myfirst_poll       (reports POLLIN/POLLOUT readiness)
    -> myfirst_close      (per-fh dtor releases per-fh state)

kldunload
    -> myfirst_detach     (refuses if any descriptor open)
    -> wakeup releases sleepers
    -> destroy_dev
    -> cbuf_destroy
    -> sysctl_ctx_free
    -> mtx_destroy
```

### File Layout Summary

```text
examples/part-02/ch10-handling-io-efficiently/
    README.md
    cbuf-userland/
        cbuf.h
        cbuf.c
        cb_test.c
        Makefile
    stage2-circular/
        cbuf.h
        cbuf.c
        myfirst.c
        Makefile
    stage3-blocking/
        cbuf.h
        cbuf.c
        myfirst.c
        Makefile
    stage4-poll-refactor/
        cbuf.h
        cbuf.c
        myfirst.c
        Makefile
    stage5-mmap/
        cbuf.h
        cbuf.c
        myfirst.c
        Makefile
    userland/
        rw_myfirst_v2.c
        rw_myfirst_nb.c
        producer_consumer.c
        stress_rw.c
        mmap_myfirst.c
        Makefile
```

Each stage directory is independent; you can `make` and `kldload` any of them without touching the others. The userland tools are shared across all stages.

### A Mental Model in One Paragraph

The driver owns a circular buffer, protected by a single mutex. A reader holds the mutex while transferring bytes out of the buffer into a stack-resident bounce, releases the mutex, copies the bounce into user space with `uiomove`, wakes any waiting writers, and loops until the user's request is satisfied or the buffer is empty. A writer mirrors this: holds the mutex, copies user bytes into a bounce, releases the mutex, copies the bounce into the buffer, wakes any waiting readers, and loops. When either the buffer is empty (for reads) or full (for writes), the handler either sleeps with the mutex as interlock (default mode) or returns `EAGAIN` (non-blocking mode). `select(2)` and `poll(2)` integration is provided through `selrecord` (in `d_poll`) and `selwakeup` (in the I/O handlers). The detach path waits for all descriptors to close and then frees everything.

That paragraph fits in your head. Everything else in this chapter is the careful elaboration of how to make each piece of it work.

## Appendix: A Source-Reading Walkthrough of `evdev/cdev.c`

The chapter has pointed at `/usr/src/sys/dev/evdev/cdev.c` several times as the cleanest example in the tree of a character device that does what `myfirst` now does: a per-client ring buffer, blocking reads, non-blocking support, `select`/`poll`/`kqueue` integration. Reading that file once, with the patterns from this chapter in hand, is the fastest way to confirm that the kernel really does work the way the chapter has been describing. This appendix walks through the relevant pieces.

The goal is *not* to teach `evdev`. It is to use `evdev` as an exhibit. By the end of this walkthrough you should feel that what you built in `myfirst` is the same shape as what the kernel uses for real input devices. The differences are in the details (the protocol, the structures, the layered driver stack), not in the underlying patterns.

### What `evdev` Is

`evdev` is FreeBSD's port of the Linux event-device interface. It exposes input devices (keyboards, mice, touchscreens) through `/dev/input/eventN` nodes that user-space programs (X servers, Wayland compositors, console handlers) read from to get a stream of input events. Each event is a fixed-size structure with a timestamp, a type, a code, and a value.

The driver layer that interests us is the per-client cdev. When a process opens `/dev/input/event0`, the kernel creates a `struct evdev_client` for that descriptor, attaches it to the underlying device, and uses it as the per-open buffer. Reads pull events out of the buffer; writes push events into it (for some devices); `select`/`poll`/`kqueue` report when events are available.

That description should sound very familiar by now. It is the same architecture as `myfirst` Stage 4, with three differences: the buffer is per-descriptor rather than per-device; the unit of transfer is a fixed-size structure rather than a byte; and the driver participates in a larger framework of input handling.

### The Per-Client State

Open `/usr/src/sys/dev/evdev/evdev_private.h` (the file is short; you can read the relevant parts in a couple of minutes). The key structure is `struct evdev_client`:

```c
struct evdev_client {
        struct evdev_dev *      ec_evdev;
        struct mtx              ec_buffer_mtx;
        size_t                  ec_buffer_size;
        size_t                  ec_buffer_head;
        size_t                  ec_buffer_tail;
        size_t                  ec_buffer_ready;
        ...
        bool                    ec_blocked;
        bool                    ec_revoked;
        ...
        struct selinfo          ec_selp;
        struct sigio *          ec_sigio;
        ...
        struct input_event      ec_buffer[];
};
```

Compare this against your `myfirst` softc:

- `ec_evdev` is `evdev`'s analogue of `dev->si_drv1` (a back-pointer from the per-client state to the device-wide state).
- `ec_buffer_mtx` is the per-client mutex; `myfirst`'s `sc->mtx` is per-device.
- `ec_buffer_size`, `ec_buffer_head`, `ec_buffer_tail`, `ec_buffer_ready` are the circular-buffer indices. Notice that `evdev` uses an explicit `tail` instead of a derived one; the code is slightly different but the structure is the same.
- `ec_blocked` is a hint flag for the wakeup logic.
- `ec_revoked` flags a forcibly-disconnected client; this is the equivalent of `is_attached` in `myfirst`.
- `ec_selp` is the `selinfo` for `select`/`poll`/`kqueue` support, exactly like `sc->rsel` and `sc->wsel` in your driver (combined here because evdev only does read-readiness; there is no concept of "write would block").
- `ec_buffer[]` is the flexible-array-member that holds the actual events.

The patterns are the same. The naming is different.

### The Read Handler

Open `/usr/src/sys/dev/evdev/cdev.c` and find `evdev_read`:

```c
static int
evdev_read(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct evdev_dev *evdev = dev->si_drv1;
        struct evdev_client *client;
        ...
        ret = devfs_get_cdevpriv((void **)&client);
        if (ret != 0)
                return (ret);

        debugf(client, "read %zd bytes by thread %d", uio->uio_resid,
            uio->uio_td->td_tid);

        if (client->ec_revoked)
                return (ENODEV);

        ...
        if (uio->uio_resid != 0 && uio->uio_resid < evsize)
                return (EINVAL);

        remaining = uio->uio_resid / evsize;

        EVDEV_CLIENT_LOCKQ(client);

        if (EVDEV_CLIENT_EMPTYQ(client)) {
                if (ioflag & O_NONBLOCK)
                        ret = EWOULDBLOCK;
                else {
                        if (remaining != 0) {
                                client->ec_blocked = true;
                                ret = mtx_sleep(client, &client->ec_buffer_mtx,
                                    PCATCH, "evread", 0);
                                if (ret == 0 && client->ec_revoked)
                                        ret = ENODEV;
                        }
                }
        }

        while (ret == 0 && !EVDEV_CLIENT_EMPTYQ(client) && remaining > 0) {
                head = client->ec_buffer + client->ec_buffer_head;
                ...
                bcopy(head, &event.t, evsize);

                client->ec_buffer_head =
                    (client->ec_buffer_head + 1) % client->ec_buffer_size;
                remaining--;

                EVDEV_CLIENT_UNLOCKQ(client);
                ret = uiomove(&event, evsize, uio);
                EVDEV_CLIENT_LOCKQ(client);
        }

        EVDEV_CLIENT_UNLOCKQ(client);

        return (ret);
}
```

Walk through it slowly.

The handler retrieves the per-client state with `devfs_get_cdevpriv`, exactly as `myfirst_read` does. The `ec_revoked` check is `evdev`'s equivalent of `myfirst`'s `is_attached` check, except that `evdev` returns `ENODEV` rather than `ENXIO` (both are valid choices for "the device is gone").

The handler then validates that the requested transfer size is a multiple of an event-record size (because partial events make no sense to deliver). This is a layer above what `myfirst` does, and it is specific to event-stream devices.

Then, exactly as Section 5 described, the handler enters a *check-wait-recheck* loop. If the buffer is empty (`EVDEV_CLIENT_EMPTYQ(client)`), the handler either returns `EWOULDBLOCK` (the same value as `EAGAIN`) for non-blocking callers, or sleeps with `mtx_sleep` and `PCATCH`. The sleep channel is `client` itself; the mutex interlock is `&client->ec_buffer_mtx`. When the sleep returns, the handler re-checks `ec_revoked`, returning `ENODEV` if the client has been disconnected during the sleep.

After the wait, the handler enters the transfer loop. It picks an event off the buffer (with `bcopy` into a stack-resident `event` variable), advances `ec_buffer_head` modulo the buffer size, releases the mutex, and calls `uiomove` to push the event to user space. Then it reacquires the mutex and continues until either the buffer empties or the user's request is satisfied.

This is the bounce-buffer pattern from Section 3, with `event` playing the role of `bounce`. The cbuf operation is `bcopy(head, &event.t, evsize)` (single-event copy out of the ring) followed by `uiomove(&event, evsize, uio)` (transfer to user). The mutex is held only across the cbuf operation, never across `uiomove`. This is exactly the rule we made operational in `myfirst`.

### The Wakeup

Find `evdev_notify_event` (the function that gets called when a new event is delivered into a client's buffer):

```c
void
evdev_notify_event(struct evdev_client *client)
{

        EVDEV_CLIENT_LOCKQ_ASSERT(client);

        if (client->ec_blocked) {
                client->ec_blocked = false;
                wakeup(client);
        }
        if (client->ec_selected) {
                client->ec_selected = false;
                selwakeup(&client->ec_selp);
        }

        KNOTE_LOCKED(&client->ec_selp.si_note, 0);
}
```

There is the wake-up. `wakeup(client)` matches the `mtx_sleep(client, ...)` from `evdev_read`. `selwakeup(&client->ec_selp)` matches the `selrecord` we will look at in a moment. `KNOTE_LOCKED` is the `kqueue` analogue of `selwakeup`; we have not built that yet (it is Chapter 11 territory) but the pattern is the same.

The `ec_blocked` flag is an optimisation: if no client is currently sleeping, the wakeup is skipped. This is a small but useful optimisation. `myfirst` does not have it because the cost is negligible in our use case, but you could add the same check trivially.

### The Poll

Find `evdev_poll`:

```c
static int
evdev_poll(struct cdev *dev, int events, struct thread *td)
{
        struct evdev_client *client;
        int revents = 0;
        int ret;

        ret = devfs_get_cdevpriv((void **)&client);
        if (ret != 0)
                return (POLLNVAL);

        if (events & (POLLIN | POLLRDNORM)) {
                EVDEV_CLIENT_LOCKQ(client);
                if (!EVDEV_CLIENT_EMPTYQ(client))
                        revents = events & (POLLIN | POLLRDNORM);
                else {
                        client->ec_selected = true;
                        selrecord(td, &client->ec_selp);
                }
                EVDEV_CLIENT_UNLOCKQ(client);
        }

        return (revents);
}
```

This is essentially identical to the `myfirst_poll` from Section 5, with two differences. `evdev` only handles `POLLIN`; there is no `POLLOUT` because input events are unidirectional. And `evdev` returns `POLLNVAL` if `devfs_get_cdevpriv` fails, which is the conventional "this descriptor is invalid" response (compared to `myfirst`'s simpler return-zero approach).

The pattern is the one Section 5 introduced: check the condition, return ready if it is true, register with `selrecord` if it is not. The `ec_selected` flag is again a wakeup-elision optimisation; you can ignore it for understanding.

### The kqfilter

Find `evdev_kqfilter`:

```c
static int
evdev_kqfilter(struct cdev *dev, struct knote *kn)
{
        struct evdev_client *client;
        int ret;

        ret = devfs_get_cdevpriv((void **)&client);
        if (ret != 0)
                return (ret);

        switch(kn->kn_filter) {
        case EVFILT_READ:
                kn->kn_fop = &evdev_cdev_filterops;
                break;
        default:
                return(EINVAL);
        }
        kn->kn_hook = (caddr_t)client;

        knlist_add(&client->ec_selp.si_note, kn, 0);

        return (0);
}
```

This is the `kqueue` registration handler. It is one of Chapter 11's topics; we are showing it here only to point out that the `selinfo`'s `si_note` field is what `kqueue` hooks into. The `selrecord`/`selwakeup` machinery for `select`/`poll`, and the `knlist_add`/`KNOTE_LOCKED` machinery for `kqueue`, share the same `selinfo` structure. That sharing is what lets a single set of state-change calls (in `evdev_notify_event`) wake all three readiness-notification paths at once.

When we extend `myfirst` with `kqueue` support in Chapter 11, the changes will fit into roughly the same pattern: a `myfirst_kqfilter` handler that registers against `&sc->rsel.si_note`, a `KNOTE_LOCKED(&sc->rsel.si_note, 0)` call alongside each `selwakeup(&sc->rsel)`. The substrate is here already.

### What the Walkthrough Confirms

Three things should be clear now.

The first is that the *patterns* you have been building are not invented for this book. They are the patterns the kernel uses, in a real driver that ships with FreeBSD and is exercised by every keyboard and mouse user every day. You can read this code, recognise what each section is doing, and explain it. That is a real skill that pays off in every later chapter.

The second is that the *details* differ between drivers. `evdev` uses an explicit `tail` index. It uses fixed-size event records instead of bytes. It has per-client buffers instead of per-device. It uses `bcopy` instead of a cbuf abstraction. None of these differences invalidate the underlying pattern; they are choices about how to specialise the pattern for a specific use case.

The third is that *you can read more*. With a couple of hours and a coffee, you can work through `/usr/src/sys/dev/uart/uart_core.c` or `/usr/src/sys/dev/snp/snp.c`. Each one will look different at first glance, but the buffer, the locking, the sleep / wake, the poll: those will be familiar. The chapter has handed you a vocabulary; the kernel source is where you exercise it.

### A Short Reading Plan

If you want to build the habit of reading kernel source as part of your driver development workflow, here is a short plan. Spend an hour a week, for three weeks, on the following.

Week one: re-read `/usr/src/sys/dev/null/null.c` and `/usr/src/sys/dev/evdev/cdev.c`. Compare them. The first is the simplest possible character device; the second is a competent buffered one. Note exactly which features each file has and why.

Week two: read `/usr/src/sys/dev/random/randomdev.c`. It is bigger than `evdev` but uses the same patterns, with the addition of an entropy-collection layer underneath. Note how `randomdev_read` differs from `evdev_read` and `myfirst_read`, and why.

Week three: pick a driver in `/usr/src/sys/dev/` that interests you (a USB driver, a network driver, a storage driver). Read the part of it that handles user-space I/O. By now the patterns should be familiar enough that the unfamiliar parts (bus binding, hardware register access, DMA setup) stand out as the *new* things to learn, not as obstacles to understanding the I/O path.

After three weeks of this rhythm, you will have read more driver code than most professional kernel developers do in a typical month. The investment compounds.

## Chapter Summary

This chapter took the in-kernel buffer from Chapter 9, which was a linear FIFO that wasted half its capacity, and turned it into a real circular buffer with proper partial-I/O semantics, blocking and non-blocking modes, and `poll(2)` integration. The driver you finish the chapter with is meaningfully better than the one you started with, in four specific ways.

It uses its full capacity. The circular buffer keeps the entire allocation in play. A steady writer and a matching reader can keep the buffer at any fill level indefinitely; the wrap-around is invisible to the I/O handlers because it lives inside the cbuf abstraction.

It honours partial transfers. Both `myfirst_read` and `myfirst_write` loop until they cannot make progress, then return zero with `uio_resid` reflecting the unconsumed portion. A user-space caller that loops on `read(2)` or `write(2)` will see correct UNIX semantics. A caller that does not loop will still see correct counts; the driver does not silently throw bytes away.

It blocks correctly. A reader that finds the buffer empty sleeps on a clear channel, with the mutex released atomically; a writer that adds bytes wakes the sleeper. The same pattern works in the other direction. Signals are honoured through `PCATCH`, so the user's Ctrl-C interrupts a blocked reader within microseconds.

It supports non-blocking mode. A descriptor opened with `O_NONBLOCK` (or with the flag set later via `fcntl(2)`) sees `EAGAIN` instead of a sleep. A `d_poll` handler reports `POLLIN` and `POLLOUT` correctly based on the buffer's state, and `selrecord(9)` plus `selwakeup(9)` ensure that `select(2)` and `poll(2)` callers wake when readiness changes.

Each of those capabilities is built up in a numbered section, each with code that compiles, loads, and behaves predictably. The chapter's stages (a userland buffer, a kernel splice, a blocking-aware version, a poll-aware refactor, a memory-mapped variant) form a clear progression that matches the order in which a beginner naturally encounters these concerns.

Along the way we covered three supplementary topics that often appear next to buffered I/O in real drivers: `d_mmap(9)`, the patterns and limitations of zero-copy thinking, and the readahead and write-coalescing patterns used by high-throughput drivers. None of these is a Chapter 10 topic in its own right, but each one builds naturally on the buffer abstraction you have just put in place.

We exercised the driver with five new user-space test programs (`rw_myfirst_v2`, `rw_myfirst_nb`, `producer_consumer`, `stress_rw`, `mmap_myfirst`) plus the standard base-system tools (`dd`, `cat`, `hexdump`, `truss`, `sysctl`, `vmstat`). The combination is enough to catch most of the bugs this material typically produces. The Troubleshooting section catalogues those bugs with their symptoms and fixes; keep it bookmarked.

Finally, we refactored the buffer access into a small set of helpers (`myfirst_buf_read`, `myfirst_buf_write`, `myfirst_wait_data`, `myfirst_wait_room`), wrote a one-paragraph locking-strategy comment near the top of the source, and put the cbuf into its own file with its own `MALLOC_DEFINE`. The driver's source is now in the shape that Chapter 11 wants: clear locking discipline, narrow abstractions, no surprises.

## Wrapping Up

The buffered driver you have just finished is the foundation for everything that follows in Part 3. The shape of its I/O path, the discipline of its locking, the way it sleeps and wakes, the way it composes with `poll(2)`: these are not Chapter 10 patterns. They are *the* patterns the kernel uses, and once you recognise them in your own code, you will recognise them in every character-device driver in `/usr/src/sys/dev/`.

It is worth taking a moment to feel that shift. When you started Chapter 7, the inside of a driver was probably an opaque set of names and signatures. By the end of Chapter 8 you had a sense of the lifecycle. By the end of Chapter 9 you had a sense of the data path. Now, at the end of Chapter 10, you have a sense of how a driver behaves under load: how it shapes itself around concurrent callers, how it manages a finite resource (the buffer), how it cooperates with the kernel's readiness primitives, how it gets out of its own way so that user-space programs can do real work against it.

Most of what you have built will carry through to Chapter 11. The mutex, the buffer, the helpers, the locking strategy, the test kit, the lab discipline: all of it remains. What changes in Chapter 11 is the *depth* of the questions you ask about each piece. Why one mutex and not two? Why `wakeup` and not `wakeup_one`? Why `mtx_sleep` and not `cv_wait`? What guarantees does the kernel make about when a sleeper wakes, and what guarantees does it not make? How do you prove a piece of code is correct under concurrency, rather than hope?

Chapter 11 takes those questions seriously. It introduces `WITNESS` and `INVARIANTS` as the kernel's verification tools, walks through the lock classes, and discusses the patterns that turn merely-working concurrency into provably-correct concurrency. It will be a substantial chapter, but the substrate is what you have just built.

Three closing reminders before you move on.

The first is to *commit your code*. Whatever version-control system you use, save the four stage directories as a snapshot. The next chapter's first lab will copy your Stage 4 source and modify it; you do not want to lose the working baseline.

The second is to *try the labs*. Reading driver code teaches you the patterns; writing driver code teaches you the discipline. The labs in this chapter are short on purpose. Even the long ones can be done in a single sitting. The combination of "I built this" and "I broke it on purpose to see what happens" is what the chapter is designed to produce.

The third is to *trust the slow path*. The chapter has been deliberately careful, deliberately patient, deliberately repetitive in places. Driver work rewards that style. The bugs that hurt are the ones that look like they could not possibly happen. The defence against them is to be slow, careful, and methodical, even when the code seems simple. The reader who slows down with each step finishes Chapter 11 ready for Chapter 12; the reader who rushes finishes Chapter 11 with a kernel panic and a lost afternoon.

You are doing well. Keep going.

## Part 2 Checkpoint

Before you cross into Part 3, pause and check that the ground beneath your feet is solid. Part 2 has carried you from "what is a module" to "a multi-stage pseudo-driver that serves real readers and writers under load." The next part will put that driver on a much heavier scale, so the foundation needs to be firm.

By now you should be comfortable doing each of the following without hunting for the answer:

- Writing, compiling, loading, and unloading a kernel module against a running kernel, and reading `dmesg` to confirm the lifecycle.
- Building a Newbus skeleton with `device_probe`, `device_attach`, and `device_detach`, backed by a per-unit softc allocated with `device_get_softc`.
- Exposing a `/dev` node through a `cdevsw` with working `d_open`, `d_close`, `d_read`, `d_write`, and `d_ioctl` handlers, and verifying that `devfs` cleans the node up on unload.
- Managing a circular buffer whose state is protected by a mutex, whose readers can block with `mtx_sleep` and be woken by `wakeup`, and whose readiness is advertised through `selrecord` and `selwakeup`.
- Walking a deliberate failure through the attach path and watching every allocation unwind in reverse order.

If any of those feels unsteady, the labs that anchor them are worth a second pass. A targeted review list:

- Build, load, and unload discipline: Lab 7.2 (Build, Load, and Verify Lifecycle) and Lab 7.4 (Simulate Attach Failure and Verify Unwinding).
- `cdevsw` hygiene and `devfs` nodes: Lab 8.1 (Structured Name and Tighter Permissions) and Lab 8.5 (Two-Node Driver).
- Data path and round-trip behaviour: Lab 9.2 (Exercise Stage 2 with Writes and Reads) and Lab 9.3 (Stage 3 FIFO Behaviour).
- The Chapter 10 core sequence: Lab 2 (Stage 2 Circular Buffer), Lab 3 (Stage 3 Blocking and Non-Blocking), and Lab 4 (Stage 4 Poll Support and Refactor).

Part 3 will assume that all of the above is muscle memory, not a lookup. Specifically, Chapter 11 will expect:

- A working Stage 4 `myfirst` that loads, unloads, and survives concurrent readers and writers without corruption.
- Familiarity with `mtx_sleep`/`wakeup` and the `selrecord`/`selwakeup` pair as the kernel's basic blocking and readiness primitives, since Part 3 will compare and contrast them with `cv(9)`, `sx(9)`, and `sema(9)`.
- A kernel built with `INVARIANTS` and `WITNESS`, since every Part 3 chapter leans on both from the first section onward.

If those three items hold, you are ready to turn the page. If one wobbles, fix that first. A quiet hour now saves a bewildering afternoon later.

## Looking Ahead: Bridge to Chapter 11

Chapter 11 is titled "Concurrency in Drivers." Its job is to take the driver you have just finished and look at it through the lens of concurrency: not the casual "it works under modest load" sense we have used so far, but the rigorous "I can prove this is correct under any interleaving" sense.

The bridge is built on three observations from Chapter 10's work.

First, you already have a single mutex protecting all shared state. That is the simplest non-trivial concurrency design a driver can have, and it is the right starting point for understanding the more elaborate alternatives. Chapter 11 will use your driver as a test case for asking when one mutex is enough, when it is not, and what to do when it is not.

Second, you already have a sleep / wake pattern that uses the mutex as an interlock. `mtx_sleep` and `wakeup` are the building blocks of every blocking primitive in the kernel. Chapter 11 will introduce condition variables (`cv_*`) as a more structured alternative, and will explain when each is appropriate.

Third, you already have a buffer abstraction that is intentionally not thread-safe by itself. The cbuf relies on the caller to provide locking. Chapter 11 will discuss the spectrum from "data structure provides no locking" (your cbuf) through "data structure provides internal locking" (some kernel primitives) to "data structure is lock-free" (`buf_ring(9)`, `epoch(9)`-based readers). Each end of the spectrum has uses; understanding when to choose which is part of becoming a driver writer.

Specific topics Chapter 11 will cover include:

- The five FreeBSD lock classes (`mtx`, `sx`, `rw`, `rm`, `lockmgr`) and when each is appropriate.
- Lock ordering and how to use `WITNESS` to verify it.
- The interaction between locks and interrupt context.
- Condition variables and when to prefer them over `mtx_sleep`.
- Reader/writer locks and their use cases.
- The `epoch(9)` framework for read-mostly data structures.
- Atomic operations (`atomic_*`) and when they obviate the need for locks.
- A walk-through of common concurrency bugs (lost wakeups, lock-order reversals, ABA, double-free under contention).

You do not need to read ahead to start Chapter 11. Everything in this chapter is sufficient preparation. Bring your Stage 4 driver, your test kit, and your `WITNESS`-enabled kernel; the next chapter starts where this one ended.

A small farewell from this chapter: you have just turned a beginner driver into a respectable one. The bytes that flow through `/dev/myfirst` are now flowing the way bytes flow through every other character device on the system. The patterns are right, the locking is right, the user-space contracts are honoured. The driver is yours to extend, to specialise, and to use as a baseline for whatever real device comes next. Take a moment to enjoy that, and then turn the page.
