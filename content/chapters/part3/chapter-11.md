---
title: "Concurrency in Drivers"
description: "What concurrency really is inside a driver, where it comes from, how it breaks code, and the first two primitives the FreeBSD kernel gives you to tame it: atomics and mutexes."
partNumber: 3
partName: "Concurrency and Synchronization"
chapter: 11
lastUpdated: "2026-04-18"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "TBD"
estimatedReadTime: 210
---

# Concurrency in Drivers

## Reader Guidance & Outcomes

Chapter 10 ended on a quietly optimistic note. Your `myfirst` driver now owns a real circular buffer, handles partial reads and writes correctly, sleeps when there is nothing to do, wakes when there is something to do, integrates with `select(2)` and `poll(2)`, and can be stress-tested with `dd`, `cat`, and a small userland harness. If you load it today and exercise it with the Stage 4 test kit, it behaves well. The byte counts match. The checksums agree. `dmesg` stays quiet.

It is tempting, looking at that behaviour, to conclude that the driver is correct. It is *almost* correct. What is missing is not code. What is missing is an explanation of *why* the code we have works.

Pause on that. In every chapter until now, we have justified each choice in the code by showing what it does: `uiomove(9)` moves bytes through the trust boundary, `mtx_sleep(9)` releases the mutex and sleeps, `selwakeup(9)` notifies `poll(2)` waiters. The chapters proved that the code is reasonable. They did not prove that the code is *safe under concurrent access*. Chapter 10 gestured at the subject; it acquired the mutex around buffer reads and writes, it released the mutex before calling `uiomove(9)`, it honoured the rule that one must not sleep while holding a non-sleepable mutex. But those moves were presented as patterns to imitate, not as conclusions to a careful argument. The argument is this chapter's work.

This chapter is where concurrency becomes the main subject.

By the end of it, you will understand why the mutex in the softc is there; what would go wrong if it were not; what a race condition actually is, down to the instruction level; what atomic operations buy you and what they do not; how mutexes compose with sleep, with interrupts, and with the kernel's correctness-checking tools; and how to tell, by reading a driver, whether its concurrency story is honest. You will also do what Chapter 11 of this book has always been meant to do: take the driver you have been building and bring it into proper, documented, verifiable thread safety. We will not turn your driver upside down; the shape we have is good. What we will do is earn every one of its locks.

This chapter does not try to teach the entire FreeBSD synchronization toolkit. That would be unreasonable at this point in your learning. Chapter 12 is dedicated to the rest of the toolkit: condition variables, sleepable shared-exclusive locks, timeouts, and the deeper aspects of lock ordering. Chapter 11 is the conceptual chapter that makes Chapter 12 possible. We stay close to two primitives, atomics and mutexes, and we spend the chapter learning them well.

### Why This Chapter Earns Its Own Place

It would be possible, in principle, to skip directly to Chapter 12 and just teach sleepable locks, condition variables, and the `WITNESS` framework in one burst. Many driver tutorials do this. The result is code that works on the author's laptop and fails on a reader's four-core desktop, because the reader does not understand what the primitives are for. They imitate the shape and miss the substance.

The cost of that kind of misunderstanding is high. Concurrency bugs are the hardest class of bug there is. They reproduce intermittently. They may appear on one machine and not another. They can sit dormant in a driver for years before a scheduling change, a faster CPU, or a new kernel option exposes them. When they do strike, the symptom is almost always somewhere other than the cause. The reader who has read only the syntax of `mtx_lock` will stare at a panic for days before realising that the bug is two functions away, in a place where nobody thought concurrency was a concern.

The only reliable defence against this is a clear mental model, built from first principles. We build that model in this chapter, one careful step at a time. By the end, `mtx_lock` and `mtx_unlock` will not be lines you copy from another driver. They will be lines whose absence you can spot.

### Where Chapter 10 Left the Driver

A quick recap of the state you should be working from. If any of the following is missing from your driver, stop here and return to Chapter 10 before continuing.

- A Newbus child registered under `nexus0`, probed and attached at module load.
- A `struct myfirst_softc` holding a `struct cbuf` (the circular buffer), per-descriptor statistics, and a single `struct mtx` called `sc->mtx`.
- Two `struct selinfo` fields (`sc->rsel` and `sc->wsel`) for `poll(2)` integration.
- `myfirst_read`, `myfirst_write`, `myfirst_poll` handlers that hold `sc->mtx` while touching shared state and drop it before calling `uiomove(9)` or `selwakeup(9)`.
- `mtx_sleep(9)` with `PCATCH` as the blocking primitive, `wakeup(9)` as the matching call.
- A detach path that refuses to unload while descriptors are open and that wakes sleepers before freeing state.

That driver is the substrate Chapter 11 reasons about. We will not change its observable behaviour in this chapter. We will understand *why* it works, we will make its safety claims explicit, and we will add the infrastructure that lets the kernel verify them for us at runtime.

### What You Will Learn

Walking away from this chapter, you should be able to:

- Define concurrency in the specific sense that matters inside a driver, and distinguish it from parallelism in a way that survives contact with real code.
- Enumerate the execution contexts a driver encounters on a modern FreeBSD system and reason about which of them can run simultaneously.
- Name the four most common failure modes of unsynchronized shared state and recognise each of them in a short code excerpt.
- Walk through a deliberately broken driver that corrupts its own buffer under load, and explain the corruption in terms of which instruction from which thread wins the race.
- Perform a concurrency audit on a piece of driver code: identify the shared state, mark the critical sections, classify each access, and decide what protects it.
- Explain what atomic operations are at the level the hardware provides them, which atomic primitives FreeBSD exposes through `atomic(9)`, when they are sufficient for the problem at hand, and when they are not.
- Use the `counter(9)` API for per-CPU statistics that scale under contention.
- Distinguish between the two fundamental mutex shapes in FreeBSD (`MTX_DEF` and `MTX_SPIN`), explain when each is appropriate, and describe the rules that apply to each.
- Read and interpret a `WITNESS` warning, understand what lock-order reversal means, and design a lock hierarchy that `WITNESS` will accept.
- State and apply the rule about sleeping with a mutex held, including the subtle cases involving `uiomove(9)`, `copyin(9)`, and `copyout(9)`.
- Describe priority inversion, priority propagation, and how the kernel handles the former with the latter.
- Build a small library of multi-threaded and multi-process test programs that can exercise your driver at levels of concurrency far beyond what `cat`, `dd`, and a single shell can produce.
- Use `INVARIANTS`, `WITNESS`, `mtx_assert(9)`, `KASSERT`, and `dtrace(1)` together to verify that the claims you make in comments are the claims the code actually enforces.
- Refactor the driver into a shape where the locking discipline is documented, the lock holder of every piece of shared state is obvious from the source, and a future reader (including your future self) can audit the concurrency story in minutes.

That is a substantial list. It is also the realistic scope of what a careful reader can absorb in one chapter at this stage of the book. Take it slowly.

### What This Chapter Does Not Cover

Several topics touch on concurrency and are deliberately postponed:

- **Condition variables (`cv(9)`).** Chapter 12 introduces `cv_wait`, `cv_signal`, `cv_broadcast`, and `cv_timedwait`, and explains when a named condition variable is a cleaner design than a bare `wakeup(9)` channel. This chapter continues to use `mtx_sleep` and `wakeup`, exactly as Chapter 10 did.
- **Sleepable shared-exclusive locks (`sx(9)`) and reader/writer locks (`rw(9)`, `rm(9)`).** Chapter 12 covers these primitives and the situations they are designed for. For Chapter 11 we stay with the mutex pair we already use.
- **Lock-free data structures.** The FreeBSD kernel uses lock-free queues (`buf_ring(9)`), `epoch(9)`-based read-mostly readers, and hazard-pointer-like patterns in several places. These are specialised tools that reward careful study and are introduced gradually in Part 4 and the Part 6 network chapter (Chapter 28) when real drivers need them.
- **Concurrency correctness under interrupt context.** Our driver has no real hardware interrupts yet. Chapter 14 introduces interrupt handlers in detail; that chapter also extends the concurrency discussion to cover what happens when a handler can run at any instant and what that means for locking choices. Chapter 11 restricts itself to process context and kernel threads.
- **Advanced `WITNESS` configuration.** We will load `WITNESS` in a debug kernel and read its warnings. We will not construct custom lock classes or explore the full complement of `WITNESS` options.

Staying inside those lines keeps the chapter honest. A chapter on concurrency that tries to teach every synchronization primitive in FreeBSD is a chapter that teaches none of them well. We teach two primitives, we teach them well, and we set up the next chapter to teach the rest from a position of strength.

### Estimated Time Investment

- **Reading only**: two and a half to three hours, depending on how often you pause at the diagrams. This chapter has a heavier conceptual load than Chapter 10.
- **Reading plus typing the labs and the broken-and-fixed drivers**: six to eight hours over two sessions with a reboot in between.
- **Reading plus all labs and challenges**: ten to fourteen hours over three or four sessions. The challenges are explicitly designed to stretch your understanding, not merely to consolidate it.

Do not rush. If you feel unsure midway through Section 4, that is normal; atomic operations are a genuinely new mental model for most readers. Stop, stretch, and come back. The value of this chapter compounds when the reader takes it slowly.

### Prerequisites

Before starting this chapter, confirm:

- Your driver source is equivalent to the Chapter 10 Stage 4 example in `examples/part-02/ch10-handling-io-efficiently/stage4-poll-refactor/`. If it is not, return to Chapter 10 and bring it up to that shape. The code in this chapter assumes the Stage 4 source.
- Your lab machine is running FreeBSD 14.3 with a matching `/usr/src`. The APIs and file paths cited here align with that release.
- You have a development kernel configuration available that enables `INVARIANTS`, `WITNESS`, and `DDB`. If you do not, Section 7 walks you through building one.
- You are comfortable loading and unloading your own module, watching `dmesg`, and reading `sysctl dev.myfirst.0.stats` to check driver state.
- You read Chapter 10 carefully and understood why `mtx_sleep` takes the mutex as its interlock. If that rule still feels arbitrary, pause and re-read Chapter 10's Section 5 before starting this chapter.

If any of the above is shaky, fixing it now is a better investment than pushing through Chapter 11 with gaps.

### How to Get the Most Out of This Chapter

Three habits pay off quickly.

First, keep `/usr/src/sys/kern/kern_mutex.c` and `/usr/src/sys/kern/subr_witness.c` bookmarked. The first is where the mutex primitives live; the second is where the kernel's lock-order checker lives. You do not need to read these files in full, but several times during the chapter we will point at a specific function and ask you to look at the real code for one minute. That minute pays dividends.

Second, read `/usr/src/sys/sys/mutex.h` once, now, before you start Section 5. The header is short, the comments are good, and seeing the full `MTX_*` flag table once makes the later discussion concrete instead of abstract.

Third, build and boot a debug kernel before Lab 1. The labs in this chapter assume `INVARIANTS` and `WITNESS` are enabled, and several of them are designed to produce specific warnings that you can only see with those options. If you have to rebuild your kernel midway through the chapter, the interruption will cost you more than doing it upfront.

### Roadmap Through the Chapter

The sections in order are:

1. Why concurrency matters. Where concurrency comes from inside the kernel, why it is not the same thing as parallelism, and what the four fundamental failure modes look like.
2. Race conditions and data corruption. A careful dissection of what a race condition is, at the instruction level, with a worked example on the circular buffer and a deliberately unsafe version of the driver you will actually run.
3. Analyzing unsafe code in your driver. A practical audit procedure. We walk through the Chapter 10 Stage 4 source, identify every piece of shared state, classify its visibility, and document what protects it.
4. Atomic operations. What atomicity is at the hardware level, what the `atomic(9)` API offers, how memory ordering affects correctness, and when atomics are sufficient versus when they are not. We apply atomics to the driver's per-descriptor statistics.
5. Introducing locks (mutexes). The two fundamental mutex shapes, the rules that apply to each, lock hierarchy, priority inversion, deadlocks, and the sleep-with-mutex rule. We re-examine the Chapter 10 mutex choice with the full conceptual toolkit.
6. Testing multi-threaded access. How to build user-space programs that exercise your driver concurrently, using `fork(2)`, `pthread(3)`, and custom harnesses. How to log results from concurrent tests without disturbing them.
7. Debugging and verifying thread safety. Reading a `WITNESS` warning, using `INVARIANTS`, adding `mtx_assert(9)` and `KASSERT(9)` calls, and tracing with `dtrace(1)`.
8. Refactoring and versioning. Organising the code for clarity, versioning the driver, documenting the locking strategy in a README, running static analysis, and regression testing.

Hands-on labs and challenge exercises follow, then a troubleshooting reference, a wrapping-up section, and a bridge to Chapter 12.

If this is your first pass, read linearly and do the labs in order. If you are revisiting the material to consolidate, the debugging section and the refactoring section stand alone.



## Section 1: Why Concurrency Matters

Every chapter up to this point has treated the driver as if it lived inside a single timeline. A user program calls `read(2)`; the kernel routes the call into `myfirst_read`; the handler does its work; control returns; the program continues. A second `read(2)` on another descriptor is described as "another call" as if it were somehow later in the same story.

This framing was an approximation. It let us focus on the data path without getting tangled in the question of who was calling our handler and when. The approximation served well for Chapters 7 through 10, because the guard rails we put in (a single mutex, careful lock acquisition around shared state, `mtx_sleep` as the only way to wait) were strong enough to hide the messiness of real kernel execution.

The messiness is not something we invented. It is the actual environment every FreeBSD driver lives in, all the time. This section is where we pull back the curtain.

### What Concurrency Means in the Kernel

**Concurrency** is the property of a system in which multiple independent streams of execution can make progress during overlapping periods of time. The operative word is *independent*: each stream has its own stack, its own instruction pointer, its own register state, and its own reason for doing what it is doing. They may share some data (that is why we care about concurrency), but none of them is waiting for any other to finish a full task before it begins.

Inside the FreeBSD kernel, these independent streams of execution come from several distinct sources. A driver will typically encounter at least four of them, possibly all of them at once:

**User-space processes calling into the driver.** This is the source you have seen most often in the book so far. A user program opens `/dev/myfirst`, issues a `read(2)` or `write(2)`, and the syscall path enters the driver through devfs. Each such call is a separate stream of execution, with its own kernel stack, running on whichever CPU the scheduler assigned it to. Two concurrent callers are two concurrent streams, no less independent of each other than two unrelated processes running in user space.

**Kernel threads spawned by the driver or the subsystem.** Some drivers create their own kernel threads with `kproc_create(9)` or `kthread_add(9)` to do work that is periodic, background, or long-running. The `myfirst` driver does not yet do this, but many real drivers do; a USB bus driver has a hub-polling thread, a network driver has a TX-complete thread, a storage driver has an I/O completion thread. Each of these runs concurrently with the syscall handlers.

**Callout and taskqueue execution.** The kernel's `callout(9)` mechanism (Chapter 13) and its `taskqueue(9)` mechanism (Chapter 16) both run functions at times that are not directly triggered by a user syscall. A callout set for one second from now will fire one second from now, regardless of what the driver's handlers are doing at that moment. The handler runs on a different stack, often on a different CPU, and may touch the same state the syscall handler is touching.

**Interrupt context.** When real hardware is present, an interrupt handler (Chapter 14) runs in response to an external event: a network packet arrived, a timer fired, a USB transfer completed. Interrupts are asynchronous and preemptive: the interrupt handler can start running at any instant the hardware chooses. The code it preempts may itself be in the middle of a critical update to shared state. `myfirst` has no real hardware yet, but we will discuss interrupt context because it shapes some of the decisions we make.

A fully-deployed FreeBSD driver typically interacts with at least three of the four. A driver writer who treats any one of them as "the only thing running" has written a driver that will fail on contact with real load.

The practical consequence is that every line of your driver code is surrounded by other code that may also be running. When your `myfirst_read` is holding `sc->mtx`, it is perfectly ordinary for a different CPU to be trying to acquire the same mutex from `myfirst_write` or from a timer or from a separate caller. The kernel's job is to make that interaction safe; your driver's job is to cooperate. That cooperation is what this chapter teaches.

### Where the Streams Come From: a Concrete Picture

It helps to look at a concrete scenario. Imagine you have a FreeBSD 14.3 machine with four CPU cores. You have loaded the Chapter 10 Stage 4 `myfirst` driver. You start the following five commands, each in its own terminal, at roughly the same time:

1. `cat /dev/myfirst > /tmp/out1.txt`
2. `cat /dev/myfirst > /tmp/out2.txt`
3. `dd if=/dev/zero of=/dev/myfirst bs=512 count=10000`
4. `sysctl -w dev.myfirst.0.debug=1` (if debug is enabled)
5. `while true; do sysctl dev.myfirst.0.stats; sleep 0.1; done`

At one instant in time, here is what the kernel might be doing:

- CPU 0 is running the first `cat`, inside `myfirst_read`, holding `sc->mtx`, performing a `cbuf_read` into its stack bounce buffer.
- CPU 1 is running the second `cat`, inside `myfirst_read`, waiting in `mtx_sleep(&sc->cb, &sc->mtx, ...)` because it tried to acquire the mutex after the first `cat` and went to sleep.
- CPU 2 is running `dd`, inside `myfirst_write`, having just finished a `uiomove` into its bounce buffer and is now about to reacquire `sc->mtx` to deposit the bytes into the cbuf.
- CPU 3 is running the `sysctl` loop, inside the `cb_used` sysctl handler, trying to acquire `sc->mtx` to get a consistent snapshot.

Four CPUs. Four streams of execution. Four different parts of the driver, all running concurrently, all contending for the same mutex, all needing to agree on the state of the same circular buffer. This is not a contrived situation. This is what a moderately-loaded system looks like during ordinary testing.

If the mutex and the rules around it did not exist, each of those four threads would be free to observe and modify the cbuf's indices (`cb_head`, `cb_used`) and its backing memory (`cb_data`) independently. The results would be, approximately, whatever the hardware's memory system happened to produce, with no guarantees about correctness. The rest of this chapter is about building an understanding of why that would be a catastrophe and what the primitives that prevent it actually do.

### Concurrency vs Parallelism

These two words are often used interchangeably. They are related but not the same, and getting them straight early saves confusion later.

**Parallelism** is a property of execution: two things happen literally at the same physical time, on different pieces of hardware. On a multi-CPU system, two threads running on two different cores at the same nanosecond are parallel.

**Concurrency** is a property of design: the system is structured so that multiple independent streams of execution exist and can make progress in any order. Whether they physically run at the same time is a separate question.

A single-core CPU can run a concurrent program (the OS scheduler interleaves two threads on the one core). It cannot run a parallel program (nothing is literally simultaneous). A four-core machine can run both concurrent and parallel programs.

Why does the distinction matter for a driver writer? Because the bugs that matter for us are caused by concurrency, not by parallelism. A two-thread program on a single-core FreeBSD laptop can still exhibit every race condition we will meet in this chapter, as long as the scheduler ever decides to preempt one thread while it is in the middle of an update. Parallelism, on a multi-core machine, merely makes those bugs more common and harder to suppress by running the program slowly. The fundamental problem predates multi-core hardware and will exist on every machine that can ever preempt a thread.

Our focus, therefore, is on designs that tolerate any legal interleaving of independent streams. If the design tolerates that, it tolerates parallelism as well. If the design assumes "well, this will always run before that", it will fail on some system, probably the first production server it meets.

This is also why you cannot test your way to concurrency correctness. You can test and not find a bug because the interleaving that triggers it never happened during the test. You can deploy the same code and find the bug the first day, because some tiny change in scheduling, or a slightly faster CPU, or a different number of cores, shifts the timing. The only durable way to solve concurrency is to reason about every legal interleaving, not to try to enumerate the likely ones.

### What Can Go Wrong: The Four Failure Modes

When multiple streams of execution share data without coordination, four things can go wrong. Every concurrency bug you will ever meet is a specialised instance of one of these four. Naming them early is useful, because later in the chapter, when a specific bug shows up in a specific driver, you will recognise which family it belongs to.

**Failure mode 1: Data corruption.** The state after the interleaving is not the state any of the threads would have produced on its own. A shared counter becomes a value that no correct sequence of increments could have reached. A linked list has an entry that points to garbage. A buffer's `head` index is larger than its capacity. This is the most common kind of concurrency bug, and the hardest to detect, because the corrupted state may lie dormant for a long time before it is used.

**Failure mode 2: Lost updates.** Two threads each read a shared value, each compute a new value based on what they read, and each store their result. The second write overwrites the first, which means the first thread's update is discarded as if it never happened. Classic example: two `bytes_read += got;` statements, executed by two threads, can increment the counter by one instead of by two if the interleaving is unlucky. Lost updates are a special case of data corruption with a predictable signature: the counter is correct in form but low in magnitude.

**Failure mode 3: Torn values.** A value that is larger than the CPU can read or write in a single memory access (on 32-bit systems, typically a 64-bit integer) can be observed in a half-updated state by a concurrent reader. One thread is in the middle of writing bytes 0-3 of a `uint64_t`; another thread reads all 8 bytes and sees the old high half glued to the new low half. The resulting value is nonsense. On FreeBSD 14.3's primary platforms (amd64, arm64), aligned 64-bit accesses are atomic; on 32-bit platforms and for larger structures, they are not. A torn value is the "read" side of a lost update and can sometimes be observed even when the writers are correct.

**Failure mode 4: Inconsistent composite state.** A data structure has an invariant that relates several fields. A thread is in the middle of updating one field but has not yet updated the other. A concurrent reader sees the structure in a state that violates the invariant, makes a decision based on what it saw, and produces a wrong result. This is the most subtle of the four modes, because it requires the observer to know what invariant is supposed to hold; a thread that does not know the invariant cannot detect that it is violated. Most of the harder driver bugs involve inconsistent composite state: a ring buffer's head and tail were updated in two steps, and a reader saw the half-updated state and produced garbage.

All four failure modes share a cause: *multiple streams of execution touched shared state without agreement on an order*. The primitives this chapter introduces, atomics and mutexes, are different ways of imposing that agreement. Atomics make individual operations indivisible. Mutexes enforce serialized access across longer regions of code.

### A Thought Experiment

Consider a trivial shared counter:

```c
static u_int shared_counter = 0;

/* Called by anything. */
static void
bump(void)
{
        shared_counter = shared_counter + 1;
}
```

If two threads call `bump()` at the same moment, what happens?

The correct behaviour is that `shared_counter` increases by two. Each call should add one, so two calls should add two.

The actual behaviour depends on how the increment compiles. On amd64, `shared_counter = shared_counter + 1;` typically compiles into three machine instructions: a load of the current value into a register, an add of one to the register, and a store of the register back to memory.

Imagine two threads, A and B, running on two different CPUs, both entering `bump()` at essentially the same time. A legal interleaving looks like this:

| Time | Thread A                 | Thread B                 | Memory |
|------|--------------------------|--------------------------|--------|
| t0   |                          |                          | 0      |
| t1   | load (gets 0)            |                          | 0      |
| t2   |                          | load (gets 0)            | 0      |
| t3   | add 1 (local register=1) |                          | 0      |
| t4   |                          | add 1 (local register=1) | 0      |
| t5   | store (writes 1)         |                          | 1      |
| t6   |                          | store (writes 1)         | 1      |

Both threads load zero. Both compute one. Both store one. The final value is one. The counter has been incremented exactly once, even though two threads each called `bump()`.

This is a lost update. It is the simplest concurrency bug in the book. It does not require fancy hardware, a complicated data structure, or a rare scheduler decision. It requires only that two threads execute the three-instruction sequence at overlapping moments.

Notice three things about this example.

First, the bug is not in the source code. The C code says `shared_counter = shared_counter + 1;`, which is what we wanted. The bug is in the *interaction between the source code and the hardware's execution model*. No amount of staring at the C line will reveal the bug, because the bug is at a level of abstraction the C line does not express.

Second, the bug is not reproducible by single-threaded testing. One thread calling `bump()` a thousand times will always produce `shared_counter == 1000`. Add a second thread and the result becomes nondeterministic. You could run the two-thread test a hundred times and see correct results every time; run it again on a different CPU, or with a different workload, and see a missed count.

Third, the fix is not to add more careful code in C. The fix is either to make the increment *atomic* (execute as a single indivisible unit from memory's perspective) or to enforce *mutual exclusion* (no two threads can be executing the three-instruction sequence at the same time). Atomics give us the first option. Mutexes give us the second. They are different shapes of solution to the same problem, and choosing between them is the topic of Sections 4 and 5.

### A Preview of the Driver Under Load

To make the rest of the chapter concrete, let us describe what we are going to do to the driver.

In Section 2, we will build a deliberately unsafe version of `myfirst`: one that has had the mutex removed from the I/O handlers. We will load it, run a two-process test, and watch the circular buffer's invariants break in real time. This will be an unpleasant but instructive experience. You will see the data corruption, the torn values, and the inconsistent composite state with your own eyes, in your own terminal, in a driver you have been working on for several chapters.

In Section 3, we will go back to the Stage 4 Chapter 10 driver and audit it: every shared field, every access, every claim. We will annotate the source in place. We will end the section with a complete locking-discipline document.

In Section 4, we will introduce atomics. We will convert `sc->bytes_read` and `sc->bytes_written` from mutex-protected `uint64_t` counters into per-CPU `counter(9)` counters, which are lock-free and scale to many CPUs. We will discuss where atomics save locks and where they do not.

In Section 5, we will re-examine the mutex from first principles. We will explain why the mutex is specifically an `MTX_DEF` (sleep) mutex, not an `MTX_SPIN` mutex. We will walk through priority inversion and why the kernel's priority-propagation mechanism handles it for us. We will introduce `WITNESS` and run the unsafe driver under a `WITNESS` kernel to see what happens.

In Section 6, we will build a small testing harness: a `pthread`-based multi-threaded tester, a `fork`-based multi-process tester, and a load generator that stresses the driver at levels `dd` and `cat` cannot reach. We will run the safe and the unsafe drivers under this harness.

In Section 7, we will learn to read `WITNESS` warnings, use `INVARIANTS` and `KASSERT`, and deploy `dtrace(1)` probes to observe concurrency behaviour without changing it.

In Section 8, we will refactor the driver for clarity, version it as `v0.4-concurrency`, write a README that documents the locking strategy, run `clang --analyze` against the source, and run a regression test that repeats every test we have built.

By the end, the driver will not be more *functional* than it is now. It will be more *verifiable*. The difference matters: verifiable code is the only code that is safe to extend.

### The FreeBSD Scheduler in One Page

To understand why concurrency inside a driver is not just a theoretical concern, it helps to know, at a high level, what the FreeBSD scheduler does. You do not need to read any scheduler source to write a driver, but a mental model of how the scheduler interacts with your driver prevents several classes of surprise.

FreeBSD's default scheduler is **ULE**, implemented in `/usr/src/sys/kern/sched_ule.c`. ULE is a multi-core, SMP-aware, priority-based scheduler. Its job is to decide, at every scheduling decision, which thread should run on each CPU.

The key properties you care about as a driver writer:

**Preemption is permitted at almost any point.** A thread executing kernel code can be preempted by a higher-priority thread becoming runnable (for example, an interrupt wakes a high-priority thread). The only windows where preemption is disabled are critical sections (entered with `critical_enter`) and spin-locked regions. Inside an `MTX_DEF` mutex's critical section, preemption is still possible; the priority propagation mechanism limits the damage.

**Threads migrate between CPUs.** The scheduler will move threads to balance load. A driver cannot assume that two successive calls from the same process run on the same CPU.

**Wake-up latency is low but not zero.** A `wakeup` on a sleeping thread typically makes the thread runnable within microseconds. The thread actually runs when the scheduler chooses to run it, which depends on its priority and what else is runnable.

**The scheduler itself holds locks.** When you call `mtx_sleep`, the scheduler's internal locks are acquired at some point. This is the reason `WITNESS` sometimes reports order violations when you hold a driver lock while doing something that reaches the scheduler; the ordering between driver locks and scheduler locks matters.

For our driver, this means:

- Two concurrent callers of `myfirst_read` may be running on two different CPUs, or on the same CPU via preemption, or sequentially. We make no assumption about which.
- The mutex we use works on all of these scenarios; the scheduler guarantees that the lock acquire/release semantics are respected regardless of CPU assignment.
- When `mtx_sleep` is called, the thread is put to sleep quickly; when `wakeup` is called, the thread becomes runnable quickly. Latency is not a concern for correctness.

Knowing the scheduler is there, doing its work in the background, frees you to focus on the driver's own concurrency decisions. You do not have to "schedule" your own threads or worry about affinity; the kernel handles it.

### Kinds of Concurrency by Pedagogy Level

For a beginner, the concurrency landscape can feel enormous. A short hierarchy helps.

**Level 0: Single-threaded.** No concurrency at all. One thread, one execution timeline. This is the model the reader has used implicitly through the first half of this book.

**Level 1: Pre-emptive concurrency on a single CPU.** One CPU, but the scheduler can interrupt a thread at any time and run another. Race conditions can still happen, because a thread can be in the middle of a read-modify-write when preempted. All the bugs this chapter discusses are possible at Level 1.

**Level 2: True multi-CPU parallelism.** Multiple CPUs, multiple threads literally running at the same physical moment. Race conditions are more common and harder to suppress with luck, but they are the same bugs as Level 1.

**Level 3: Concurrency with interrupt context.** Same as Level 2, but with the addition that interrupt handlers can preempt ordinary threads at any moment. Spin mutexes become necessary for state shared between top-half code and interrupt handlers. Chapter 14 and beyond cover this.

**Level 4: Concurrency with RCU-style patterns.** Same as Level 3, but with advanced patterns like `epoch(9)` that allow readers to proceed without any synchronization. Specialised and not needed for most drivers.

For Chapter 11, we are at Level 2. We have multiple CPUs, we have many threads, we have ordinary mutex-based coordination. This is where most drivers live.

### Some Concurrency Is Invisible

A subtle point that trips beginners: the concurrency in a driver is not limited to "user processes making syscalls". Several sources produce concurrent execution in your handlers:

- **Multiple open descriptors on the same device.** Each descriptor has its own `struct myfirst_fh`, but the device-wide `struct myfirst_softc` is shared. Two threads with different descriptors can concurrently enter the same handlers on the same softc.

- **Multiple threads sharing a descriptor.** Two pthreads in the same process can both call `read(2)` on the same file descriptor. Both enter the same handlers with the *same* `fh`. This is the subtler case, and the audit in Section 3 addresses it.

- **Sysctl readers.** A user running `sysctl dev.myfirst.0.stats` enters the sysctl handlers from a different thread than the I/O handlers. The handlers observe the same shared state.

- **Kernel threads.** If a future version of the driver spawns its own kernel thread (for timeouts, background work, etc.), that thread is a new source of concurrent access.

- **Callouts.** Similarly, a callout handler runs on its own, independently of the I/O handlers.

Every one of these is a concurrent stream. Every one must be accounted for in the locking strategy. The audit procedure from Section 3 explicitly lists them as possible access sources for each shared field.

### Wrapping Up Section 1

Concurrency is the environment every FreeBSD driver lives in, whether the driver writer has thought about it or not. The streams of execution come from user syscalls, kernel threads, callouts, and interrupts. On a multi-core machine they are literally parallel; on any machine they are concurrent, which is what the bugs care about. When multiple streams touch shared state without coordination, four families of failure emerge: data corruption, lost updates, torn values, and inconsistent composite state.

We have not yet said what to do about any of it. The next two sections do the negative work, examining what goes wrong in precise detail. Section 4 starts the positive work, introducing the first primitive that can help. Section 5 introduces the second and more general one.

Take a break here if you need one. The next section is denser.



## Section 2: Race Conditions and Data Corruption

Section 1 described the problem at the level of intuition. This section makes it concrete. We will define a race condition precisely, walk through how one arises in the circular buffer from Chapter 10, build a deliberately unsafe version of the driver that exhibits the bug, and run it on a real system. Watching the bug with your own eyes is the fastest way to develop the instinct you need for the rest of the book.

### A Precise Definition

A **race condition** is a situation in which two or more streams of execution can access a piece of shared state in overlapping windows of time, at least one of those accesses is a write, and the correctness of the system depends on the exact order of those accesses.

Three properties make that definition do real work. The first is *overlap*: the accesses do not have to be literally simultaneous, they only have to have windows that touch. A read that starts at time t and ends at time t+10, and a write that starts at t+5 and ends at t+15, are overlapping. The second is *at least one write*: two concurrent reads of the same value are fine, because neither changes the state the other is reading. The third is *order-dependent correctness*: if the code is designed so that any interleaving produces the same outcome, there is no race by this definition, even if the windows overlap.

The first property is why software-level "it's too fast to collide" reasoning never works. A read and a write are not two points; they are two intervals. Any overlap is enough.

The second property is why code that is read-only is safe without synchronization. If nothing is written, no interleaving can cause inconsistency. This is useful later when we talk about `epoch(9)` and lock-free reading, but the same observation justifies why certain purely-informational operations do not need protection.

The third property is why locks are not always the answer. If the operation is designed so that every legal interleaving is acceptable, no lock is needed. A counter that is only ever incremented by atomic operations, for example, does not need a lock around each increment. The lock would still work, but it would be wasted cycles.

### The Anatomy of a Race

Let us take the counter example from Section 1 and walk through it at the instruction level. The C statement:

```c
shared_counter = shared_counter + 1;
```

On amd64, with ordinary compilation, this produces something close to:

```text
movl    shared_counter(%rip), %eax   ; load shared_counter into register EAX
addl    $1, %eax                     ; increment EAX by 1
movl    %eax, shared_counter(%rip)   ; store EAX back to shared_counter
```

The memory access happens at two distinct moments: the load and the store. Between them, the value being computed is held in the CPU's register file, not in memory. Any other CPU observing memory during that interval sees the old value, not the value in flight.

If two threads, A and B, execute this sequence on two CPUs at overlapping times, the memory controller sees four accesses, in some order:

1. A's load
2. B's load
3. A's store
4. B's store

The final value of `shared_counter` depends entirely on the order in which the stores commit. If A's store happens first and B's store happens second, the final value is 1 (because B's store writes the value B computed, which was based on B's load, which saw 0). If the order is reversed, the final value is still 1. There is no interleaving in which both increments take effect, because the reads happened before either write, and each write overwrites the other.

The fix is to make the whole sequence either atomic (so that the four accesses become two inseparable transactions) or mutually exclusive (so that one thread finishes all three instructions before the other starts any of them). Both are legal answers. The difference is what they cost and what they allow us to do elsewhere in the code.

### How Races Appear in the Circular Buffer

The shared counter is the simplest possible example. The circular buffer from Chapter 10 is a richer one. It has multiple fields, interrelated invariants, and operations that touch several fields at once. This is where race conditions become really dangerous.

Recall the `cbuf` from Chapter 10:

```c
struct cbuf {
        char    *cb_data;
        size_t   cb_size;
        size_t   cb_head;
        size_t   cb_used;
};
```

The invariants we depend on are:

- `cb_head < cb_size` (the head index is always valid).
- `cb_used <= cb_size` (the live byte count never exceeds capacity).
- The bytes at positions `[cb_head, cb_head + cb_used) mod cb_size` are the live bytes.

These invariants relate multiple fields. A `cbuf_write` call, for example, updates `cb_used`. A `cbuf_read` call updates both `cb_head` and `cb_used`. If a reader and a writer execute concurrently without coordination, each of those updates can be observed in a half-finished state.

Consider what happens if `myfirst_read` and `myfirst_write` run concurrently on two CPUs, with no mutex:

1. Reader thread starts `cbuf_read` on a buffer with `cb_head = 1000, cb_used = 2000`. It computes `first = MIN(n, cb_size - cb_head)` and begins the first `memcpy` from `cb_data + 1000` into its stack bounce.
2. Meanwhile, writer thread calls `cbuf_write`, which updates `cb_used`. The writer reads `cb_used = 2000`, computes the tail as `(1000 + 2000) % cb_size = 3000`, does its `memcpy` into the buffer, and writes `cb_used = 2100`.
3. The reader finishes its first `memcpy`, possibly seeing bytes that the writer had just written (if the writer's memcpy crossed over the reader's position).
4. The reader then updates `cb_head = (1000 + n) % cb_size` and decrements `cb_used`.

Several things can go wrong here:

- **Torn `cb_used`**: on a 32-bit machine, `cb_used` is a 32-bit type, so its read and write are atomic. On amd64, `size_t` is 64-bit, and aligned 64-bit accesses are also atomic, so a torn read is unlikely. But if we ever used a structure larger than the word size, we would see bytes from the old value glued to bytes from the new.
- **Observed invariant violation**: the reader's decrement of `cb_used` and the writer's increment of `cb_used` happen at different moments. Between them, `cb_used` may transiently reflect only one of the two operations. If a third reader (for example, a `sysctl` handler) observes `cb_used` in the middle of that interleaving, the observed value is neither the "before" nor the "after" of either individual operation.
- **Concurrent memcpy into overlapping regions**: if the reader's memcpy and the writer's memcpy touch the same byte, the byte's final value is whatever wins the race for memory. The reader may see half of its intended data overwritten with new data from the writer. The writer may overwrite bytes that the reader has already copied into its bounce, meaning the reader returns bytes that do not exist in the buffer any more.

Each of those is a real bug. Each is hard to observe, because the timing window for the corruption is tiny. Each is lethal, because a single instance can corrupt the data a user program depends on.

### A Deliberately Unsafe Driver

Theory becomes visceral when you run the code. We are going to build a version of `myfirst` that has the mutex removed from the I/O handlers. You will load it, run a concurrent test against it, and watch the driver produce nonsense.

**Warning:** load this driver only on a lab machine with no important state. It can corrupt its own state at will and, under some scenarios, wedge the machine. Expect to reboot if the test goes badly. The point is to see the bug, not to push it further.

Create a new directory alongside your Chapter 10 Stage 4 source:

```sh
$ cd examples/part-03/ch11-concurrency
$ mkdir -p stage1-race-demo
$ cd stage1-race-demo
```

Copy `cbuf.c`, `cbuf.h`, and `Makefile` from the Chapter 10 Stage 4 directory. Then copy `myfirst.c` and edit it to remove the mutex from `myfirst_read` and `myfirst_write`. The easiest way to do this, so you can see exactly what changed, is to use `sed` with a sentinel:

```sh
$ sed \
    -e 's/mtx_lock(&sc->mtx);/\/\* RACE: mtx_lock removed *\//g' \
    -e 's/mtx_unlock(&sc->mtx);/\/\* RACE: mtx_unlock removed *\//g' \
    ../../part-02/ch10-handling-io-efficiently/stage4-poll-refactor/myfirst.c \
    > myfirst.c
```

What this does is turn every `mtx_lock` and `mtx_unlock` in the source into a comment. It leaves the rest of the code unchanged, including `mtx_sleep` and `mtx_assert`. This is a surgical deletion of the lock acquisition points; the rest of the driver still thinks the lock is there.

Before we build, open `myfirst.c` and look for the `mtx_assert(&sc->mtx, MA_OWNED)` calls inside `myfirst_buf_read`, `myfirst_buf_write`, and the wait helpers. These are going to fire a `KASSERT` panic on an `INVARIANTS` kernel, because the helpers check that the mutex is held and now it never is. This is a feature, not a bug: if you accidentally run this driver on a debug kernel, the assertion will catch the problem before any real damage is done. For the explicit race demo, we want to see the corruption itself, so comment out the `mtx_assert` lines or disable `INVARIANTS` in the test kernel:

```sh
$ sed -i '' -e 's|mtx_assert(&sc->mtx, MA_OWNED);|/* RACE: mtx_assert removed */|g' myfirst.c
```

The `-i ''` is FreeBSD's `sed` in-place mode. On Linux, it would be `sed -i` without the quoted empty string.

Also delete the `mtx_init`, `mtx_destroy`, and `mtx_lock`/`mtx_unlock` calls from `myfirst_attach` and `myfirst_detach` by hand; the `sed` commands above do not catch the attach and detach paths because those have `mtx_lock` calls we want to keep logically (even if they do nothing). A simpler and cleaner approach, if you prefer, is to introduce a macro at the top of `myfirst.c`:

```c
/* DANGEROUS: deliberately unsafe driver for Chapter 11 Section 2 demo. */
#define RACE_DEMO

#ifdef RACE_DEMO
#define MYFIRST_LOCK(sc)        do { (void)(sc); } while (0)
#define MYFIRST_UNLOCK(sc)      do { (void)(sc); } while (0)
#define MYFIRST_ASSERT(sc)      do { (void)(sc); } while (0)
#else
#define MYFIRST_LOCK(sc)        mtx_lock(&(sc)->mtx)
#define MYFIRST_UNLOCK(sc)      mtx_unlock(&(sc)->mtx)
#define MYFIRST_ASSERT(sc)      mtx_assert(&(sc)->mtx, MA_OWNED)
#endif
```

Then globally replace `mtx_lock(&sc->mtx)` with `MYFIRST_LOCK(sc)` and similarly for the rest. With `RACE_DEMO` defined, the macros do nothing; without it, they are the original calls. This is the approach the companion source uses. It lets you toggle the race on and off at compile time.

Build the module:

```sh
$ make
$ kldstat | grep myfirst && kldunload myfirst
$ kldload ./myfirst.ko
$ dmesg | tail -5
```

The attach line should appear as usual. Nothing looks wrong yet.

### Running the Race

With the unsafe driver loaded, run the producer/consumer test from Chapter 10:

```sh
$ cd ../../part-02/ch10-handling-io-efficiently/userland
$ ./producer_consumer
```

Recall that this test produces a fixed pattern into the driver, reads it back in another process, and compares checksums. On the safe driver, the checksums match and the test reports zero mismatches. Run it against the unsafe driver and the result depends on your timing:

- **Likely outcome**: the two checksums differ, and the reader reports some number of mismatches. The exact number varies between runs.
- **Possible outcome**: the test hangs forever, because the buffer state became inconsistent and the reader or writer got stuck waiting for a condition that will never be true.
- **Rare outcome**: the test passes by chance. If this happens, run it again; a single run of a concurrency test tells you almost nothing.
- **Unpleasant outcome**: the kernel panics because of invariants fired inside `cbuf_read` or `cbuf_write` (for example, a negative `cb_used` that was meant to be unsigned, or an out-of-bounds `cb_head`). If this happens, note the backtrace, reboot, and carry on. This is what you came to see.

In most runs you will see output like:

```text
writer: 1048576 bytes, checksum 0x8bb7e44c
reader: 1043968 bytes, checksum 0x3f5a9b21, mismatches 2741
exit: reader=2 writer=0
```

The reader got fewer bytes than the writer produced. Its checksum is different. Some specific number of byte positions held the wrong value.

This is not a flaky test. This is a driver working as designed, given that the locking has been removed. The kernel schedules multiple threads onto multiple CPUs. Those threads read and write the same memory without coordination. The result is the state that any specific hardware interleaving happens to produce.

Run the test several times. The numbers will vary. The character of the corruption, however, will not: the writer will claim to have produced some number of bytes, the reader will see a different set. That is what concurrency without synchronization looks like. It is not mysterious or subtle; it is the exact consequence of the definitions in the earlier part of this section.

### Observing the Damage with Logs

The producer-consumer test reports aggregate damage. For finer-grained observation, instrument the driver with `device_printf` (guarded by the debug sysctl) inside `cbuf_write` and `cbuf_read`. Add something like:

```c
device_printf(sc->dev,
    "cbuf_write: tail=%zu first=%zu second=%zu cb_used=%zu\n",
    tail, first, second, cb->cb_used);
```

at the bottom of `cbuf_write`, behind the `myfirst_debug` check you set up in Chapter 10 Stage 2. The same for `cbuf_read`.

Run the unsafe driver again with `sysctl dev.myfirst.debug=1` and watch `dmesg -t` in another terminal:

```sh
$ sysctl dev.myfirst.debug=1
$ ./producer_consumer
$ dmesg -t | tail -40
```

You will see lines like:

```text
myfirst0: cbuf_write: tail=3840 first=256 second=0 cb_used=2048
myfirst0: cbuf_read: head=0 first=256 second=0 cb_used=1792
myfirst0: cbuf_write: tail=1536 first=256 second=0 cb_used=2304
myfirst0: cbuf_read: head=2048 first=256 second=0 cb_used=-536  <-- inconsistent
```

`cb_used` is a `size_t`, which is unsigned, so "negative" here means the field wrapped: a very large number that was formatted with `%zu` and still looks enormous but was not meant to be. That wraparound is one of the failure modes we discussed. A concurrent decrement of `cb_used` read the value from before another thread's increment finished, subtracted its own amount, and stored a result that is nonsense.

Looking at a log full of such entries drives home the point the definitions did not: concurrency bugs are not rare edge cases. They are the normal case when multiple threads touch the same fields without coordination. The driver does not break occasionally. It breaks constantly. It just sometimes happens to produce a superficially plausible output anyway.

### Cleaning Up

Unload the unsafe driver before doing anything else:

```sh
$ kldunload myfirst
```

If `kldunload` hangs (because a test still holds a descriptor open), find and kill the test process. If the kernel is in a broken state after a panic-like failure, reboot. From here on, work only with the safe driver unless you are deliberately rerunning the demo to compare.

Do not leave the unsafe driver loaded. Do not check in the `RACE_DEMO`-enabled build and forget about it. The companion tree keeps the race demo in a sibling directory precisely so that it cannot be confused with the production source.

### The Lesson

Three observations follow from the exercise.

The first is that the mutex in the Chapter 10 driver is not stylistic. Remove it and the driver breaks, instantly, in a way you can demonstrate on any FreeBSD machine. The mutex is load-bearing. It protects exactly the property (the cbuf's internal consistency) that the rest of the driver's correctness depends on.

The second is that testing cannot find this class of bug reliably. If you did not know to run a multi-process stress test, if you tested only with a single `cat` and a single `echo`, the driver would look fine. The concurrency bug would be there, invisible, waiting for the first user whose workload happens to involve two concurrent callers. Reviewing code with concurrency in mind is not a substitute for testing; it is the thing that prevents you from shipping code whose tests happen to pass.

The third is that the fix is not a local patch. You cannot sprinkle atomic operations on a field or two and call it done. The cbuf has composite invariants that span multiple fields; protecting it requires a region of code where no other thread can be executing at the same time. That is the job of a mutex, and Section 5 is where we look at mutexes in full. Before we get there, however, we should audit the existing driver (Section 3) and understand the more limited tool that atomics provide (Section 4).

### A Second Race: Attach Versus I/O

Not every race is in the data path. Let us look at a race we fixed in Chapter 10 without naming: the race between `myfirst_attach` and the first user to call `open(2)`.

When `myfirst_attach` runs, it does several things in sequence:

1. Sets up `sc->dev` and `sc->unit`.
2. Initializes the mutex.
3. Sets `sc->is_attached = 1`.
4. Initializes the cbuf.
5. Creates the cdev via `make_dev_s`.
6. Creates the cdev alias.
7. Registers the sysctls.
8. Returns.

The cdev is not visible to user space until step 5. A user who calls `open("/dev/myfirst")` before step 5 completes gets `ENOENT`, because the device node does not exist yet. After step 5, the cdev is registered with devfs, and `open` can succeed.

If step 5 happened before step 3, a user could open the device and call `read` before `is_attached` was set. The read handler's `if (!sc->is_attached) return (ENXIO)` check would fail the call, returning ENXIO even though attach was in progress. That is not catastrophic, but it is confusing and avoidable.

The fix is the order we use: `is_attached = 1` happens before `make_dev_s`. By the time any handler can run, `is_attached` is already set.

The subtle point is that this ordering is *only correct* because single-threaded attach cannot be interrupted between the two writes. If the writes could be reordered by the compiler or the hardware (which they cannot for plain integer stores on amd64 without explicit barriers, but could on some weakly-ordered architectures), we would need an `atomic_store_rel_int` for the `is_attached` write. The book stays on amd64, so the plain store is fine.

This is a generally useful discipline. Every time you write attach code, list the observable preconditions for each subsequent step and verify they are established before the step is reachable from user space. The order matters, and it is almost always wrong to make the cdev before the softc is fully ready.

### A Third Race: sysctl Versus I/O

Another race the Chapter 10 design handles: the `sysctl` handler that reads `cb_used` or `cb_free` must return a self-consistent value, not a torn composite.

The fields `cb_head` and `cb_used` are both `size_t`. Either one, read alone, gives a single-word value that is atomic on amd64. But `cb_used` and `cb_head` together form an invariant (the live bytes are at `[cb_head, cb_head + cb_used) mod cb_size`). A sysctl that reads both without the mutex could observe them at inconsistent moments.

For `cb_used` alone, the race is tolerable: the counter may be slightly stale, but the value returned is at least *some* value that was once true. For `cb_head`, the same applies. For anything that combines the two (like "how many bytes until the tail meets the capacity boundary?"), the race produces meaningless numbers.

We protect the sysctl reads with the mutex. The critical section is tiny (one load), so contention is minimal; the correctness benefit is that the returned value is guaranteed to be self-consistent.

The rule this illustrates is: **if your observation combines multiple fields that together express an invariant, protect the observation with the same primitive that protects the invariant**.

### The Race Taxonomy, Revisited

With the cbuf race (Section 2 main text), the attach race, and the sysctl race, we now have three concrete examples to classify under the four failure modes from Section 1.

- **Cbuf data corruption**: primarily inconsistent composite state (the `cb_used`/`cb_head` invariant), with some lost updates on `cb_used` itself.
- **Attach race**: a form of inconsistent composite state, where the "is the device ready?" invariant is transiently false from the user's perspective.
- **Sysctl torn observation**: a form of inconsistent composite state, where the reader's computation uses fields from different moments in time.

All three are protected by the same mechanism: serialize access to the composite state with a single mutex. This unity is not a coincidence; it is the reason a single mutex is usually the right answer for a small driver.

### Wrapping Up Section 2

A race condition is a very specific thing: overlapping access to shared state, at least one write, correctness that depends on the interleaving. The cbuf's composite invariants make it a rich source of races when the mutex is removed. A deliberately unsafe driver reveals the corruption within seconds of running a concurrent test.

You have now seen the problem in your own terminal, in your own driver. You have seen three distinct races, all in places where composite state is involved, all solved by the same mechanism. Everything that follows is the construction of the tools that make the problem go away, one layer at a time.



## Section 3: Analyzing Unsafe Code in Your Driver

Most driver writers, most of the time, do not create race conditions deliberately. They create them by not noticing that a piece of state is shared across streams of execution. The defence against this is a habit, not a clever tool: every time you touch state that *could* be accessed from outside the current code path, you stop and ask whether it is actually safe to touch without synchronization. Most of the time the answer is "yes, it's already protected by X", and you move on. Sometimes the answer is "actually, it isn't protected, and I need to think about it". That second case is the one this section teaches you to recognise.

The Chapter 10 Stage 4 driver is reasonably thoughtful. It uses a mutex, it documents what the mutex protects, and it follows consistent conventions. But even a reasonably thoughtful driver benefits from an explicit audit. The goal of this section is to walk through the driver and classify every piece of shared state: who reads it, who writes it, what protects it, and under what conditions. The result is a small document that accompanies the code, makes the concurrency story auditable, and becomes a checkpoint you can use when future changes risk breaking the model.

### What "Shared" Means

A variable is **shared** if more than one stream of execution can access it during the same time window. "Same time window" does not require literal simultaneity; it requires that the two accesses could *potentially* overlap.

That definition is broader than beginners usually expect. Three examples clarify it.

**A local variable inside a function is not shared.** Each invocation of the function gets its own stack frame, and the variable lives in that frame. Another thread can enter the same function, but it will get its own stack and its own copy of the variable. There is no shared memory.

**A static variable inside a function is shared.** Despite the `static` keyword making it look local, the storage is file-scope: all invocations of the function see the same byte of memory. Two concurrent invocations of the function share that byte.

**A field in a dynamically allocated structure is shared if and only if more than one stream of execution has a pointer to the same structure.** The `struct myfirst_softc` for a given device is one such structure. Every call into the driver that goes through `dev->si_drv1` observes the same softc. Two concurrent calls see the same softc.

A more subtle variant: a field in a per-descriptor structure (our `struct myfirst_fh`) is shared only if more than one stream has access to the same file descriptor. Two processes with distinct descriptors do not share each other's `struct myfirst_fh`. Two threads in the same process sharing a descriptor *do*. Most driver writers treat per-descriptor state as "almost" unshared and use lighter protection than device-wide state, but the word "almost" is doing heavy lifting; we will come back to this case in Section 5.

### The Shared State of myfirst

Open your Stage 4 source. We are going to annotate, field by field, the contents of `struct myfirst_softc`. You can do this in the source itself (as a comment block near the struct declaration) or in a separate design document. I recommend doing both: a short summary in the source and a detailed version in a file you can evolve.

Here is the structure, as of Chapter 10 Stage 4:

```c
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

        struct selinfo          rsel;
        struct selinfo          wsel;

        struct cdev            *cdev;
        struct cdev            *cdev_alias;

        struct sysctl_ctx_list  sysctl_ctx;
        struct sysctl_oid      *sysctl_tree;
};
```

For each field we ask five questions:

1. **Who reads it?** Which functions, and from which streams of execution?
2. **Who writes it?** Same question, but for writes.
3. **What protects the accesses from race conditions?**
4. **What invariants does it participate in?**
5. **Is the current protection sufficient?**

Going field by field is tedious. It is also the whole point. A driver whose concurrency story is documented field by field is a driver that almost certainly works. A driver whose concurrency story is left implicit is a driver whose author hoped for the best.

**`sc->dev`**: the Newbus `device_t` handle.
- Read by: every handler (indirectly, through `sc->dev` in `device_printf` and similar).
- Written by: attach (once, during module load).
- Protection: write happens before any handler can run (the cdev is not registered until after `sc->dev` is set). After attach completes, the field is effectively immutable. No lock needed.
- Invariants: `sc->dev != NULL` once attach has returned zero.
- Sufficient? Yes.

**`sc->unit`**: a convenience copy of `device_get_unit(dev)`.
- Same analysis as `sc->dev`. Immutable after attach. No lock needed.

**`sc->mtx`**: the sleep mutex.
- "Accessed" by every handler in `mtx_lock`/`mtx_unlock`/`mtx_sleep` calls.
- Initialized by `mtx_init` in attach, destroyed by `mtx_destroy` in detach.
- Protection: the mutex protects itself; the init and destroy are sequenced relative to the cdev's creation and destruction so that no handler can ever see a half-initialized mutex.
- Invariants: the mutex is initialized after `mtx_init` returns and before `mtx_destroy` is called. Handlers only run during that window.
- Sufficient? Yes.

**`sc->attach_ticks`**: the `ticks` value at attach time, for informational purposes.
- Read by: the sysctl handler for `attach_ticks`.
- Written by: attach (once).
- Protection: write happens before any sysctl handler can be called. After that, immutable. No lock needed.
- Invariants: none beyond "set at attach".
- Sufficient? Yes.

**`sc->open_count`**: lifetime count of opens.
- Read by: the sysctl handler for `open_count`.
- Written by: `myfirst_open`.
- Protection: the write happens under `sc->mtx` in `myfirst_open`. The read, currently, is an unprotected load from the SYSCTL_ADD_U64 handler.
- Invariants: monotonically increasing.
- Sufficient? On amd64, a 64-bit aligned load is atomic, so a torn read is not possible. The sysctl may observe an old value (a load that happened just before a concurrent increment) but not a corrupted one. This is acceptable for an informational counter. On 32-bit platforms, the same load would not be atomic, and a torn read would be possible. For this book's purposes (amd64 lab machine), the current protection is sufficient. We will note this in the documentation.

**`sc->bytes_read`, `sc->bytes_written`**: lifetime byte counters.
- Same analysis as `open_count`. Written under the mutex, read unprotected from sysctl, acceptable on amd64 but not 32-bit. These are candidates for migration to `counter(9)` per-CPU counters, which would remove the torn-read concern and also scale better under contention. Section 4 does this migration.

**`sc->active_fhs`**: current count of open descriptors.
- Read by: sysctl handler, `myfirst_detach`.
- Written by: `myfirst_open`, `myfirst_fh_dtor`.
- Protection: all reads and writes happen under `sc->mtx`.
- Invariants: `active_fhs >= 0`. `active_fhs == 0` when detach is called and succeeds.
- Sufficient? Yes.

**`sc->is_attached`**: flag indicating whether the driver is in the attached state.
- Read by: every handler at entry (to fail-fast with `ENXIO` if the device has been torn down).
- Written by: attach (sets to 1), detach (sets to 0, under the mutex).
- Protection: the write in detach is under the mutex. The reads in the handlers at entry are *not* under the mutex (they are unprotected plain loads).
- Invariants: `is_attached == 1` while the device is usable.
- Sufficient? There is a subtle question here. A handler that reads `is_attached == 1` without the mutex might then proceed to acquire the mutex, and by the time it has the mutex, detach may have run and set the flag to 0. Our handlers handle this case by re-checking `is_attached` after every sleep (the `if (!sc->is_attached) return (ENXIO);` pattern after `mtx_sleep`). The entry check is an optimization that avoids taking the mutex in the common case; correctness does not depend on it. We will document this explicitly.

**`sc->cb`**: the circular buffer (all its fields).
- Read by: `myfirst_read`, `myfirst_write`, `myfirst_poll`, the sysctl handlers for `cb_used` and `cb_free`.
- Written by: `myfirst_read`, `myfirst_write`, attach (via `cbuf_init`), detach (via `cbuf_destroy`).
- Protection: every access is under `sc->mtx`, including the sysctl handlers (which take the mutex around a brief read). `cbuf_init` runs in attach before the cdev is registered, so it is effectively single-threaded. `cbuf_destroy` runs in detach after all descriptors have been confirmed closed, so it is also effectively single-threaded.
- Invariants: `cb_used <= cb_size`, `cb_head < cb_size`, the bytes at `[cb_head, cb_head + cb_used) mod cb_size` are live.
- Sufficient? Yes. This is the field the Chapter 10 work spent most of its time getting right.

**`sc->rsel`, `sc->wsel`**: `selinfo` structures for `poll(2)` integration.
- Read/written by: `selrecord(9)`, `selwakeup(9)`, `seldrain(9)`. These functions handle their own locking internally; the caller's responsibility is merely to call them at the right times.
- Protection: internal to the `selinfo` machinery. The caller holds `sc->mtx` around `selrecord` (because it is called inside `myfirst_poll`, which takes the mutex) and drops `sc->mtx` around `selwakeup` (because it may sleep).
- Invariants: the `selinfo` is initialised once (it is zero-initialized by the softc allocation) and drained at detach.
- Sufficient? Yes.

**`sc->cdev`, `sc->cdev_alias`**: pointers to the cdev and its alias.
- Read by: nothing critical; they are stored for destruction.
- Written by: attach (set), detach (destroyed and cleared).
- Protection: writes happen at known lifecycle points before or after handlers can run.
- Invariants: `sc->cdev != NULL` during the attached window.
- Sufficient? Yes.

**`sc->sysctl_ctx`, `sc->sysctl_tree`**: the sysctl context and root node.
- Managed by the sysctl framework. The framework handles its own locking.
- Protection: framework-internal.
- Sufficient? Yes.

### The Per-Descriptor State

Now the per-descriptor state, in `struct myfirst_fh`:

```c
struct myfirst_fh {
        struct myfirst_softc   *sc;
        uint64_t                reads;
        uint64_t                writes;
};
```

**`fh->sc`**: back-pointer to the softc.
- Read by: handlers that retrieve it via `devfs_get_cdevpriv`.
- Written by: `myfirst_open`, once, before the fh is handed to devfs.
- Protection: the write is sequenced before any handler can see the fh.
- Invariants: immutable after `myfirst_open`.
- Sufficient? Yes.

**`fh->reads`, `fh->writes`**: per-descriptor byte counters.
- Read by: the `myfirst_fh_dtor` destructor (for a final log message) and potentially future sysctl handlers per-descriptor (not currently exposed).
- Written by: `myfirst_read`, `myfirst_write`.
- Protection: the writes happen under `sc->mtx` (inside the read/write handlers). Two threads holding the same descriptor open could in principle write concurrently; both would hold `sc->mtx`, so the accesses serialize.

This is the subtle case noted earlier. Two threads in the same process can share a file descriptor. Both can call `read(2)` on it. Both will enter `myfirst_read`, both will retrieve the same `fh` via `devfs_get_cdevpriv`, both will want to update `fh->reads`. Because both hold `sc->mtx` during the update, the operations serialize, and no race occurs. If we ever wanted to hold less than the full `sc->mtx` (for instance, if we used a lock-free counter update), the per-descriptor case would force us to be more careful.

### Classifying the Critical Sections

With the fields inventoried, we can now identify every **critical section** in the code. A critical section is a contiguous region of code that accesses shared state and must execute without interference from concurrent streams.

Go through `myfirst.c` and find every region bounded by `mtx_lock(&sc->mtx)` and `mtx_unlock(&sc->mtx)`. Each of those regions is a critical section by construction. Then look for regions that should be critical sections but are not. There should be none in the Stage 4 driver, but the audit is still valuable for confirming the gap.

The critical sections in Stage 4 are:

1. **`myfirst_open`**: updates `open_count` and `active_fhs` under the mutex.
2. **`myfirst_fh_dtor`**: decrements `active_fhs` under the mutex.
3. **`myfirst_read`**: multiple critical sections, each bracketing an access to the cbuf, separated by the out-of-lock `uiomove` calls. The wait helper runs inside the critical section; the byte counters are updated inside; `wakeup` and `selwakeup` are called with the mutex dropped.
4. **`myfirst_write`**: the mirror of `myfirst_read`.
5. **`myfirst_poll`**: one critical section that checks the cbuf state and either sets `revents` or calls `selrecord`.
6. **The two cbuf sysctl handlers**: each takes the mutex briefly to read `cbuf_used` or `cbuf_free`.
7. **`myfirst_detach`**: acquires the mutex to check `active_fhs` and to set `is_attached = 0` before waking sleepers.

Each of these should do as little work as possible while holding the mutex. Work that does not need the mutex (the `uiomove`, for example, or any computation on stack-local values) should happen outside. The Stage 4 driver is already careful about this; the audit confirms it.

### Annotating the Source

One useful output of the audit is to go back to the source and add one-line comments above each critical section that name the shared state being protected. This is not decorative; it is documentation for the next person to read the code.

For example, the `myfirst_read` handler becomes:

```c
static int
myfirst_read(struct cdev *dev, struct uio *uio, int ioflag)
{
        /* ... entry logic ... */
        while (uio->uio_resid > 0) {
                /* critical section: cbuf state + bytes_read + fh->reads */
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

                /* not critical: wake does its own locking */
                wakeup(&sc->cb);
                selwakeup(&sc->wsel);

                /* not critical: uiomove may sleep; mutex must be dropped */
                error = uiomove(bounce, got, uio);
                if (error != 0)
                        return (error);
        }
        return (0);
}
```

Three comments. Thirty seconds of work. They transform the handler from a piece of code that a reader has to analyze to a piece of code whose concurrency story is self-evident.

### A Locking-Strategy Document

Alongside the source annotations, maintain a short document that describes the overall strategy. The file `LOCKING.md` in your driver source tree is a good location. A minimal version looks like this:

```markdown
# myfirst Locking Strategy

## Overview
The driver uses a single sleep mutex (sc->mtx) to serialize all
accesses to the circular buffer, the sleep channel, the selinfo
structures, and the per-descriptor byte counters.

## What sc->mtx Protects
- sc->cb (the circular buffer's internal state)
- sc->bytes_read, sc->bytes_written
- sc->open_count, sc->active_fhs
- sc->is_attached (writes; reads may be unprotected as an optimization)
- sc->rsel, sc->wsel (through the selinfo API; the mutex is held
  around selrecord, dropped around selwakeup)

## Locking Discipline
- Acquire with mtx_lock, release with mtx_unlock.
- Wait with mtx_sleep(&sc->cb, &sc->mtx, PCATCH, wmesg, 0).
- Wake with wakeup(&sc->cb).
- NEVER hold sc->mtx across uiomove, copyin, copyout, selwakeup,
  or wakeup. Each of these may sleep or take other locks.
- All four myfirst_buf_* helpers assert mtx_assert(MA_OWNED).

## Known Non-Locked Accesses
- sc->is_attached at handler entry: unprotected read. Safe because
  a stale true is re-checked after every sleep; a stale false is
  harmless (the handler returns ENXIO, which is also what it would
  do with a fresh false).
- sc->open_count, sc->bytes_read, sc->bytes_written at sysctl
  read time: unprotected load. Safe on amd64 (aligned 64-bit loads
  are atomic). Would be unsafe on 32-bit platforms.

## Lock Order
sc->mtx is currently the only lock the driver holds. No ordering
concerns arise. Future versions that add additional locks must
specify their order here.
```

That document is the definitive answer to "is this driver safe?". It is reviewable. It can be checked against the code. It is the output of the audit we just performed.

### A Practical Audit Procedure

The procedure we just walked through can be systematized:

1. List every field of every shared structure (softc, per-descriptor, any module-global state).
2. For each field, identify all read sites and all write sites.
3. For each field, identify the protection mechanism (lock held, lifecycle guarantee, framework-internal, atomic type, intentionally unprotected).
4. For each field, identify the invariants it participates in and the other fields those invariants involve.
5. For each invariant, confirm that the protection mechanism covers all relevant accesses.
6. For each known-unprotected access, document why it is acceptable.
7. Annotate the source code to name each critical section and each deliberate unprotected access.
8. Write the summary into a `LOCKING.md` file.

It sounds laborious. For a driver the size of `myfirst`, it takes under an hour. For a driver ten times the size, it takes a day. Either way, it pays for itself the first time it catches a bug that was not yet written.

### Common Audit Discoveries

Three patterns recur. If your audit finds any of them, treat them as red flags.

The first is **silent sharing**: a field that you thought was local but is actually accessible from outside the current code path. The usual cause is a pointer that leaks from a structure you thought was private. `devfs_set_cdevpriv` gives a cdev a pointer to a per-descriptor structure; if that structure contains a back-pointer, another thread can reach into it. Per-descriptor state that is accessed only from within the current thread is safer than state reachable from the softc.

The second is **irregular protection**: a field that is protected in most code paths but not in one. Often the unprotected path is the one added most recently. The audit catches it, the unit tests do not.

The third is **invariant drift**: over time, a field acquires new correlations with other fields, and the protection stops covering the composite invariant. The audit reveals it by asking what invariants each field participates in.

A disciplined audit, repeated periodically, catches all three.

### A Second Worked Audit: A Hypothetical Multi-Queue Driver

To exercise the audit procedure on something slightly more interesting, imagine a driver that is not `myfirst`. Suppose we are auditing a hypothetical network-adjacent pseudo-device, `netsim`, which simulates a packet source. The softc has more shared state:

```c
struct netsim_softc {
        device_t                dev;
        struct mtx              global_mtx;
        struct mtx              queue_mtx;

        struct netsim_queue     rx_queue;
        struct netsim_queue     tx_queue;

        struct callout          tick_callout;
        int                     tick_rate_hz;

        uint64_t                packets_rx;
        uint64_t                packets_tx;
        uint64_t                dropped;

        int                     is_up;
        int                     is_attached;
        int                     active_fhs;

        struct cdev            *cdev;
};
```

This is a richer structure. Let us apply the audit procedure.

**Two locks.** `global_mtx` and `queue_mtx`. We need to decide what each protects and establish an order.

A reasonable split: `global_mtx` protects configuration and lifecycle state (`is_up`, `is_attached`, `tick_rate_hz`, `active_fhs`, `tick_callout`). `queue_mtx` protects the RX and TX queues, which are the high-frequency hot path.

**Lock order.** Because configuration changes are rare and queue operations are hot, we order: `global_mtx` before `queue_mtx`. A thread that holds `global_mtx` may acquire `queue_mtx`. A thread holding `queue_mtx` may not acquire `global_mtx`.

This is a choice, not an edict. A different driver might reverse it. What matters is consistency. `WITNESS` will catch any violation.

**Counter fields.** `packets_rx`, `packets_tx`, `dropped` are counters updated on the data path. `counter(9)` is the right tool.

**The callout.** `tick_callout` fires periodically to generate packets. Its handler runs on a kernel thread, concurrently with user-driven read/write. The callout handler takes `queue_mtx` to enqueue packets. Because the callout is initialised before the cdev is registered and is stopped on detach, the callout and the user handlers are properly bounded.

**The invariants.**

- `is_attached == 1` during the attached window; detach clears it under `global_mtx`.
- The queue structures have their own internal invariants (head/tail indices); `queue_mtx` protects them.
- `tick_rate_hz` is read in the callout; it is written in attach and in ioctls; reads are under `global_mtx` so the rate cannot change mid-tick.

**What to write in LOCKING.md**:

```markdown
## Locks

### global_mtx (MTX_DEF)
Protects: is_up, is_attached, active_fhs, tick_rate_hz, tick_callout.

### queue_mtx (MTX_DEF)
Protects: rx_queue, tx_queue internal state.

## Lock Order
global_mtx -> queue_mtx

A thread holding global_mtx may acquire queue_mtx.
A thread holding queue_mtx may NOT acquire global_mtx.

## Lock-Free Fields
packets_rx, packets_tx, dropped: counter_u64_t. No lock required.

## Callout
tick_callout fires every 1/tick_rate_hz seconds. It acquires
queue_mtx to enqueue packets. It does not acquire global_mtx.
Stopping the callout is done in detach, under global_mtx, with
callout_drain.
```

Notice that the audit produced a document that looks very much like the one for `myfirst`, even though the driver is larger. The structure is the same: locks, what they protect, the order, rules, lock-free fields, notable subsystems.

That repeatability is the point of having an audit procedure. Every driver's LOCKING.md looks similar in shape, because the questions you ask during the audit are the same. Only the answers differ.

### How Audits Catch Bugs

Three concrete examples of bugs that a careful audit would catch:

**Example 1: Missing lock on an update.** A new sysctl handler is added that allows the user to change `tick_rate_hz` at runtime. The handler writes the field without taking `global_mtx`. The audit catches this: the field is protected by `global_mtx`, but one write path does not take the lock.

**Example 2: Lock order inversion.** A new function, `netsim_rebalance`, is added. It takes `queue_mtx` to inspect the queue lengths, then takes `global_mtx` to update configuration based on the lengths. This is the wrong order. The audit catches this by asking: "for every function, does its lock acquisition match the global order?".

**Example 3: Torn read of `packets_rx`.** A sysctl handler reads `packets_rx` with a plain 64-bit load. On amd64 this is fine; on 32-bit platforms it is not. The audit catches this by documenting which architectures the driver targets and flagging platform-dependent assumptions.

Each of these bugs is the kind that evades single-threaded testing. The audit catches them by being systematic.

### Audits as Change Gates

In a mature driver project, the audit becomes a change gate: any commit that modifies shared state, adds a new field, or changes a lock acquisition is required to update LOCKING.md in the same commit. Reviewers check that the update is consistent with the code change.

This discipline sounds bureaucratic. In practice, it is fast (updating the doc takes a minute) and it is the single most effective defense against concurrency regressions. A driver that ships with a correct LOCKING.md is a driver whose concurrency story can be audited by any reviewer without reading every line of the code.

For our `myfirst`, the LOCKING.md is short because the driver is small. For larger drivers, the LOCKING.md scales in proportion. The value scales too.

### Wrapping Up Section 3

You now have, for your own driver, a field-by-field account of every piece of shared state, every critical section, and every protection mechanism. You have a `LOCKING.md` file that documents the locking strategy. You have source annotations that make the concurrency intent visible to any reader. You have seen the same procedure applied to a hypothetical larger driver to show that it scales.

The rest of the chapter introduces the primitives that let you add or relax protection as your design evolves. Section 4 looks at atomics: what they buy you, what they do not, and where they fit in your driver. Section 5 is the full treatment of mutexes, including the subtleties that Chapter 10 gestured at.



## Section 4: Introduction to Atomic Operations

Atomic operations are one of the two tools this chapter introduces. They are useful when the shared state you need to protect is small and the operation you need to perform on it is simple. They do not replace mutexes for anything more complex, but they are often the right tool for the simpler cases, and they are always faster. This section builds the intuition for what atomics are, presents the `atomic(9)` API FreeBSD exposes, discusses memory ordering at a level appropriate for our purposes, and applies the `counter(9)` per-CPU counter API to the driver's statistics.

### What "Atomic" Means at the Hardware Level

Recall the counter race from Section 2. Two threads loaded, incremented, and stored the same memory location. Because the load, increment, and store were three separate instructions, another thread could slip in between them. The fix, at the simplest level, is to use a single instruction that performs all three operations indivisibly: no other thread can observe memory midway through.

Modern CPUs provide such instructions. On amd64, the instruction that atomically adds a value to memory is `LOCK XADD`. The `LOCK` prefix tells the CPU to lock the relevant cache line for the duration of the instruction, so that no other CPU can touch the same line until the instruction completes. The `XADD` instruction itself performs an exchange-add: it adds its source to the destination and returns the destination's old value, all in one transaction. After `LOCK XADD`, the memory location has the correct sum, and no other CPU has been able to observe a partial update.

The C primitive that compiles to this instruction is `atomic_fetchadd_int`. FreeBSD's `atomic(9)` API exposes it, along with many siblings, in `/usr/src/sys/sys/atomic_common.h` and the per-architecture `atomic.h` headers.

**Atomicity**, then, is a property of a *memory operation*: if the operation completes, it does so in a single indivisible transaction from memory's perspective. No other CPU can observe a half-done version. This is a hardware-level guarantee, not a software abstraction. When you use an atomic primitive in C, you are telling the compiler to emit an instruction that the hardware itself guarantees is indivisible.

Three things matter about this guarantee.

First, it covers *one* operation. `atomic_fetchadd_int` does one add. Two successive atomic operations are not jointly atomic: another thread can observe memory between them. If you need two fields to be updated together, you still need a mutex.

Second, it covers *word-sized* operations, for some definition of word. On amd64, atomic primitives cover 8, 16, 32, and 64 bits. Operations on larger structures (arrays, composite structs) cannot be atomic at the hardware level; they require a mutex or a specialised mechanism like `atomic128_cmpset` on platforms that provide it. The `atomic(9)` API exposes the granularities the hardware supports.

Third, it is *cheap*. An atomic increment is typically a few cycles slower than a non-atomic one, because the cache line must be acquired exclusively. Compared to acquiring and releasing a mutex (which itself uses atomics internally and may also take scheduler turns under contention), an atomic operation is usually an order of magnitude faster. When the work you need to do is simple, atomics give you correctness at minimal cost.

### The FreeBSD atomic(9) API

The kernel exposes a family of atomic operations through function-like macros. The full set is documented in `atomic(9)` (run `man 9 atomic`). For the purposes of this chapter we care about a small subset.

The four most common operations are:

```c
void   atomic_add_int(volatile u_int *p, u_int v);
void   atomic_subtract_int(volatile u_int *p, u_int v);
u_int  atomic_fetchadd_int(volatile u_int *p, u_int v);
int    atomic_cmpset_int(volatile u_int *p, u_int expect, u_int new);
```

- `atomic_add_int(p, v)` atomically computes `*p += v`. The return value is not meaningful; the update is indivisible.
- `atomic_subtract_int(p, v)` atomically computes `*p -= v`. Same shape.
- `atomic_fetchadd_int(p, v)` atomically computes `*p += v` and returns the value that was in `*p` *before* the add. Useful when you want both to update and to observe the previous value in the same transaction.
- `atomic_cmpset_int(p, expect, new)` atomically performs a compare-and-swap: if `*p == expect`, write `new` to `*p` and return 1; otherwise leave `*p` alone and return 0. This is the fundamental primitive for building more complex lock-free data structures.

Each of these has variants for different integer widths (`_long`, `_ptr`, `_64`, `_32`, `_16`, `_8`) and for different memory-ordering requirements (`_acq`, `_rel`, `_acq_rel`). The width variants are obvious: they work on different integer types. The memory-ordering variants deserve their own subsection.

There are also read and store primitives:

```c
u_int  atomic_load_acq_int(volatile u_int *p);
void   atomic_store_rel_int(volatile u_int *p, u_int v);
```

These are atomic loads and stores with specific memory-ordering guarantees. For aligned machine-word types on our platforms, ordinary `*p` accesses are atomic in the load/store sense, but the `atomic_load_acq_int` / `atomic_store_rel_int` forms also act as memory barriers: they prevent the compiler and the CPU from reordering surrounding loads and stores past them. We will see why that matters shortly.

Finally, a few specialised primitives:

```c
void   atomic_set_int(volatile u_int *p, u_int v);
void   atomic_clear_int(volatile u_int *p, u_int v);
```

These are bitwise OR and AND-NOT respectively: `atomic_set_int(p, FLAG)` sets the `FLAG` bit in `*p`; `atomic_clear_int(p, FLAG)` clears it. They are useful for flag words where multiple threads may be setting and clearing different bits.

### A Gentle Introduction to Memory Ordering

Here is a subtlety that bites beginners: even with atomic operations, the *order in which memory accesses become visible to other CPUs* is not always the order in which the code executed them. Modern CPUs and compilers may reorder instructions for performance, as long as the reordering is invisible to the single thread that issued them. In a multi-threaded program, the reordering is sometimes visible, and it can matter.

The classic example is a producer that prepares a payload and then sets a ready flag, while a consumer spins on the flag and then reads the payload:

```c
/* Thread A (producer): */
data = compute_payload();
atomic_store_rel_int(&ready_flag, 1);

/* Thread B (consumer): */
while (atomic_load_acq_int(&ready_flag) == 0)
        ;
use_payload(data);
```

For the pattern to work, the two stores on thread A's side must become visible to thread B in order: thread B must not see `ready_flag == 1` while still seeing the old `data`. Without memory barriers, the CPU or the compiler is free to reorder those stores, and the consumer would read stale data.

The `_rel` suffix on `atomic_store_rel_int` is a **release** barrier: every write that happened before the store, in program order, is made visible to other CPUs before the store itself is. The `_acq` suffix on `atomic_load_acq_int` is an **acquire** barrier: every read that happens after the load, in program order, sees values at least as fresh as the value the load saw. Paired, release on the publisher and acquire on the consumer ensure the consumer observes everything the publisher did before the release.

We will not build lock-free data structures in this chapter; the subsection "Memory Ordering on a Multi-Core Machine" later in Section 4 walks through the ordering guarantees in a little more depth, and real lock-free patterns belong to Part 6 (Chapter 28) when specific drivers need them. The important point for now is that the same `_rel` / `_acq` pattern is the reason `mtx_lock` and `mtx_unlock` compose correctly with memory: `mtx_lock` has acquire semantics (nothing inside the critical section leaks out above it), and `mtx_unlock` has release semantics (nothing inside the critical section leaks out below it).

For a beginner, the practical takeaway is this: use the atomic primitive that corresponds to the intent of your operation. A counter increment uses the plain form. A flag that publishes the completion of other work uses the `_rel` / `_acq` pair. When in doubt, use the stronger form (`_acq_rel`); it costs a little more but is correct in more situations.

### When Atomics Suffice

Atomics are the right tool when all four of the following are true:

1. The operation you need is simple (a counter increment, a flag set, a pointer exchange).
2. The shared state is a single word-sized field (or a small number of independent ones).
3. The correctness of the code does not depend on more than one memory location being consistent with another (no composite invariants).
4. The operation is cheap enough that replacing a mutex with an atomic is an observable win.

For `myfirst`, the counters `bytes_read` and `bytes_written` meet all four criteria. They are simple increments, single fields, independent of each other, and frequently updated on the data path. Converting them from mutex-protected fields to atomic (or per-CPU) counters is a clean win.

The cbuf, on the other hand, does not meet the criteria. Its correctness depends on a composite invariant (`cb_used <= cb_size`, the bytes at `[cb_head, cb_head + cb_used)` are live) that spans multiple fields. No single atomic operation can preserve that invariant. The cbuf needs a mutex, and no amount of cleverness with atomics will change that. This is worth stating clearly because beginners sometimes try to "atomically update cb_used and cb_head" and end up with a driver that compiles, looks clever, and is still broken.

### Per-CPU Counters: the counter(9) API

FreeBSD provides, for the specific case of "a counter that is incremented frequently and read infrequently", a specialised API: `counter(9)`. A `counter_u64_t` is a per-CPU counter, where each CPU has its own private memory for the counter, and a read combines all of them.

The API is:

```c
counter_u64_t   counter_u64_alloc(int flags);
void            counter_u64_free(counter_u64_t c);
void            counter_u64_add(counter_u64_t c, int64_t v);
uint64_t        counter_u64_fetch(counter_u64_t c);
void            counter_u64_zero(counter_u64_t c);
```

`counter_u64_alloc(M_WAITOK)` returns a handle to a new per-CPU counter. `counter_u64_add(c, 1)` atomically adds 1 to the calling CPU's private copy (no cross-CPU synchronization is required). `counter_u64_fetch(c)` sums across all CPUs and returns the total. `counter_u64_free(c)` releases the counter.

The per-CPU design has two consequences. First, adds are very fast: they touch only the calling CPU's cache line, so there is no cross-CPU contention. Even on a 32-core system, a `counter_u64_add` call does not pay the cost of synchronizing with other cores. Second, reads are expensive: `counter_u64_fetch` sums across all CPUs, which costs roughly one cache-miss per CPU. Reads are therefore infrequent; updates are frequent.

This shape is exactly right for the `bytes_read` and `bytes_written` counters. They are updated on every I/O call (high rate). They are read only when a user runs `sysctl` or when detach emits a final log line (low rate). Migrating them to `counter_u64_t` gives us both correctness across 32-bit and 64-bit architectures, and scalability across many CPUs.

### Migrating the Driver's Counters

Here is what the migration looks like. First, change the fields in `struct myfirst_softc`:

```c
struct myfirst_softc {
        /* ... */
        counter_u64_t   bytes_read;
        counter_u64_t   bytes_written;
        /* ... */
};
```

Update `myfirst_attach` to allocate them:

```c
static int
myfirst_attach(device_t dev)
{
        /* ... existing setup ... */
        sc->bytes_read = counter_u64_alloc(M_WAITOK);
        sc->bytes_written = counter_u64_alloc(M_WAITOK);
        /* ... rest of attach ... */
}
```

Update `myfirst_detach` to free them:

```c
static int
myfirst_detach(device_t dev)
{
        /* ... existing teardown ... */
        counter_u64_free(sc->bytes_read);
        counter_u64_free(sc->bytes_written);
        /* ... rest of detach ... */
}
```

Update the read and write handlers to use `counter_u64_add` instead of the plain increment:

```c
counter_u64_add(sc->bytes_read, got);
```

Update the sysctl handlers to use `counter_u64_fetch`:

```c
static int
myfirst_sysctl_bytes_read(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        uint64_t v = counter_u64_fetch(sc->bytes_read);
        return (sysctl_handle_64(oidp, &v, 0, req));
}
```

And register the sysctl with a handler instead of a direct pointer:

```c
SYSCTL_ADD_PROC(&sc->sysctl_ctx, SYSCTL_CHILDREN(sc->sysctl_tree),
    OID_AUTO, "bytes_read",
    CTLTYPE_U64 | CTLFLAG_RD | CTLFLAG_MPSAFE,
    sc, 0, myfirst_sysctl_bytes_read, "QU",
    "Total bytes drained from the FIFO");
```

Once the migration is done, you can drop the `sc->bytes_read` and `sc->bytes_written` entries from the list of fields protected by `sc->mtx` in your `LOCKING.md`. They now protect themselves, in a way that scales better and is correct on any architecture FreeBSD supports.

### A Simple Atomic Flag Example

Let us also look at a smaller, more targeted use of atomics. Suppose we want to track whether the driver is "currently busy" in a coarse sense, for debugging. We do not need strict correctness; we just want a flag we can flip and read.

Add a field:

```c
volatile u_int busy_flag;
```

Update it in the handlers:

```c
atomic_set_int(&sc->busy_flag, 1);
/* ... do the work ... */
atomic_clear_int(&sc->busy_flag, 1);
```

Read it elsewhere:

```c
u_int is_busy = atomic_load_acq_int(&sc->busy_flag);
```

This is *not* a correct way to implement mutual exclusion. Two threads could both read `busy_flag == 0`, both set it to 1, both do their work, both clear it. The flag does not prevent concurrent execution; it is purely informational. For actual mutual exclusion, we need a mutex, which is the subject of the next section.

The point of the flag example is narrower: atomic operations let you set and read a single-word value without tearing. That is useful for an informational field that does not participate in an invariant. Once you need the field to participate in an invariant, atomics alone will not save you.

### The Compare-and-Swap Pattern

The most interesting atomic primitive is `atomic_cmpset_int`, which implements the **compare-and-swap** pattern. It lets you write optimistic code: "update the field if nobody else has touched it since I last looked."

The pattern is:

```c
u_int old, new;
do {
        old = atomic_load_acq_int(&sc->field);
        new = compute_new(old);
} while (!atomic_cmpset_int(&sc->field, old, new));
```

The loop reads the current value, computes what the new value should be based on it, and tries to atomically swap the new value in, conditional on the field not having changed. If another thread changed the field between the load and the cmpset, the cmpset fails and the loop retries with the new value. When it succeeds, the update is committed atomically, and `*sc->field` now holds the value derived from the state it actually held at the moment of update.

Compare-and-swap is the building block for most lock-free data structures. You can implement a lock-free stack, a lock-free queue, a lock-free counter (which is how atomic_fetchadd is often implemented under the hood), and many other structures using nothing but compare-and-swap.

For our purposes, compare-and-swap is worth knowing about even if we do not use it heavily. When you read FreeBSD source later and see `atomic_cmpset_*` in a tight loop, you will recognize the pattern immediately: optimistic retry.

### When Atomics Are Not Enough

Three situations call for a mutex even when the state looks simple.

First, when the operation involves more than one memory location. If `cb_head` and `cb_used` must be updated together, no single atomic operation can do it. Either we accept that the update is two atomics (with a transient half-updated state visible to readers), or we hold a mutex across the update.

Second, when the operation is expensive (includes a function call, a loop, or allocates memory). An atomic operation is cheap only if it is a single fast instruction. A long critical section cannot be folded into a single atomic; it needs a mutex.

Third, when the code must block. No atomic operation can put the caller to sleep. If you need to wait for a condition ("buffer has data"), you need `mtx_sleep` or a condition variable, which require a mutex.

The cbuf triggers all three. The counters trigger none. That is why the counters can migrate to atomics or `counter(9)` and the cbuf cannot.

### A Worked Example: a "driver version" Counter

Here is an exercise to ground the theory. Suppose we want to count how many times the driver has been loaded and unloaded during the current uptime. This is module-level state, not per-softc, and it is updated exactly twice per load/unload cycle: once in the module event handler when the load is successful, and once when it is unloaded.

We could protect it with a global mutex. That would be overkill: the update is one integer, happens rarely, and does not compose with anything else. An atomic is the right choice:

```c
static volatile u_int myfirst_generation = 0;

static int
myfirst_modevent(module_t m, int event, void *arg)
{
        switch (event) {
        case MOD_LOAD:
                atomic_add_int(&myfirst_generation, 1);
                return (0);
        case MOD_UNLOAD:
                return (0);
        default:
                return (EOPNOTSUPP);
        }
}
```

A reader (for example a sysctl exposing `hw.myfirst.generation`) can use:

```c
u_int gen = atomic_load_acq_int(&myfirst_generation);
```

No lock required. No contention. Correct on every architecture FreeBSD supports. This is the atomic-appropriate case: one field, one operation, no composite invariant.

### Visibility, Ordering, and the Everyday Driver

Before closing this section, one more observation about memory ordering and mutexes.

You might wonder, given that `atomic_store_rel_*` and `atomic_load_acq_*` exist, why the driver code we wrote in Chapter 10 didn't use them. The reason is that `mtx_lock` has acquire semantics built in, and `mtx_unlock` has release semantics built in. Every write inside a critical section becomes visible to the next thread that acquires the mutex, at the moment it acquires. Every read inside a critical section sees writes that happened before any previous `mtx_unlock` on the same mutex. The mutex is, among other things, a memory barrier.

So when you write:

```c
mtx_lock(&sc->mtx);
sc->field = new_value;
mtx_unlock(&sc->mtx);
```

you do not need to say `atomic_store_rel_int(&sc->field, new_value)`. The mutex already does the necessary ordering. This is an important property: it means that code inside a mutex critical section does not need to reason about memory ordering. Correctness is about the mutual exclusion, full stop.

Outside of critical sections (and only outside), you need to reason about ordering yourself. That is where the `_acq` / `_rel` atomic variants earn their keep.

For the `myfirst` driver, the only state we access outside any lock is `sc->is_attached` (the optimization at handler entry) and the per-CPU counters (which handle their own ordering). Everything else is either immutable after attach or protected by `sc->mtx`. That is a small footprint, and it is the reason our concurrency story stays manageable.

### Memory Ordering on a Multi-Core Machine

The earlier "Gentle Introduction to Memory Ordering" sketched what `_acq` and `_rel` do; this section makes the underlying mechanism a little more concrete, because once you have done one publishing pattern by hand, the rest of the kernel's atomic primitives become much easier to read.

The example is the same producer/consumer pattern with two shared variables, `payload` and `ready`. Without barriers, two orderings can fail:

- **On the producer's side**, the write to `payload` could become visible to other CPUs *after* the write to `ready`. The consumer then sees `ready == 1` while still seeing the old `payload`.
- **On the consumer's side**, the read of `payload` could be hoisted *before* the read of `ready`. The consumer commits to the old payload before it ever inspects the flag.

Why is either reordering legal? The compiler is allowed to move loads and stores past each other as long as the result is consistent for a single-thread observer. The CPU is allowed to reorder accesses through its store buffer, its prefetch unit, and its out-of-order execution engine, again as long as the local thread cannot tell. Multi-thread visibility is a property neither the compiler nor the CPU enforces by default.

The `_rel` and `_acq` suffixes lift exactly the constraints you need:

- `atomic_store_rel_int(&ready, 1)`: every store that appeared before this line in program order is made visible before the store to `ready` becomes visible. The *release* publishes everything prior.
- `atomic_load_acq_int(&ready)`: every load that appears after this line sees memory at least as fresh as the load itself saw. The *acquire* fences subsequent reads.

The pairing of release and acquire creates a *happens-before* relationship: if the consumer observes `ready == 1` through `atomic_load_acq_int`, every store the producer made before its `atomic_store_rel_int` is visible to the consumer.

In your driver, the same property is supplied by the mutex. `mtx_unlock(&sc->mtx)` has release semantics built in. `mtx_lock(&sc->mtx)` has acquire semantics. Every write made while the mutex was held is visible to the next thread that acquires the same mutex. This is why, inside a critical section, plain C assignment is fine: the mutex boundaries handle ordering for you.

Outside any lock, if you are coordinating multiple fields with atomics, you have to think explicitly about which writes need to be visible together and reach for the matching `_rel` and `_acq` variants. In the Chapter 11 driver, the only place this matters is the per-CPU counters, and `counter(9)` handles the ordering internally.

### Atomic Swaps and More Flavours

We have mentioned `atomic_add`, `atomic_fetchadd`, `atomic_cmpset`, `atomic_load_acq`, `atomic_store_rel`, `atomic_set`, and `atomic_clear`. Two more are worth naming, because you will see them in FreeBSD source.

**`atomic_swap_int(p, v)`**: atomically exchanges the value at `*p` with `v` and returns the old value. This is useful when you want to "claim" a resource: the thread that successfully swaps the flag from "free" to "taken" is the new owner.

**`atomic_readandclear_int(p)`**: atomically reads `*p` and sets it to zero, returning the old value. Useful for "drain" patterns where one thread periodically collects and resets a counter the other threads have been incrementing.

Both are built on top of the same compare-and-swap primitive the hardware provides, and both have the same cost profile as `atomic_cmpset`.

### A Practical Example: Atomic Reference Counting

A common driver pattern is reference counting: an object is allocated, several threads take references to it, and the object is freed when the last reference is dropped. Atomics are the right tool.

```c
struct myobj {
        volatile u_int refcount;
        /* ... other fields ... */
};

static void
myobj_ref(struct myobj *obj)
{
        atomic_add_int(&obj->refcount, 1);
}

static void
myobj_release(struct myobj *obj)
{
        u_int old = atomic_fetchadd_int(&obj->refcount, -1);
        KASSERT(old > 0, ("refcount went negative"));
        if (old == 1) {
                /* We were the last reference; free it. */
                free(obj, M_DEVBUF);
        }
}
```

Two details are worth noting.

The first is the use of `atomic_fetchadd_int` for the decrement. Why not `atomic_subtract_int`? Because we need to know whether *our* decrement was the one that brought the count to zero, so that we can free the object. `atomic_fetchadd_int` returns the value before the add, which tells us exactly that. An `atomic_subtract_int` does not return a value.

The second is that the free must happen *only if* our decrement was the final one. Otherwise, if two threads happen to release concurrently, they might both try to free the object. By keying the free on "old == 1", we ensure that exactly one thread (the one that took the count from 1 to 0) frees the object. All other threads simply decrement and return.

This pattern is used throughout the FreeBSD kernel. It is the reason `refcount(9)` exists as a small wrapper API (`refcount_init`, `refcount_acquire`, `refcount_release`). For our purposes we do not use reference counting yet; the example is for your toolbox.

### counter(9) in More Depth

The `counter(9)` API is used throughout the FreeBSD kernel for high-frequency counters. It is worth understanding how it works, because the design choices explain the API shape.

Each `counter_u64_t` is, internally, an array of per-CPU counters. The array is sized to the number of CPUs in the system. An add targets the calling CPU's slot. A fetch iterates over all slots and sums.

The per-CPU layout means:

- **Adds do not contend.** Two CPUs adding concurrently touch different cache lines. There is no cross-CPU traffic.
- **Fetches are expensive.** They touch every CPU's cache line, which on a 32-core machine means 32 cache misses.
- **Fetches are not atomic with adds.** A fetch might catch one CPU mid-update. The returned total may be slightly off from any instantaneous true total. This is fine for informational counters; it would be wrong for counters that feed into decisions.

For our driver, the fetches happen in sysctl handlers, which are infrequent, so the asymmetry is in our favour. If we ever needed a counter whose value must be exact at every fetch, `counter(9)` would not be the right primitive.

A subtlety: `counter_u64_zero` resets the counter to zero. Doing so while adds are happening is *not* atomic with them. If a reader fetches, sees a large value, and zeros, some adds in flight may be lost. For counters that are simply informational, this is acceptable. For counters that track budget or quota, zero carefully or not at all.

### Lock-Free Data Structures in Context

A full treatment of lock-free data structures would fill its own book. FreeBSD's kernel uses them in specific places where contention would otherwise be a bottleneck:

- **`buf_ring(9)`** is a lock-free multi-producer queue used by network and storage drivers for hot paths. The driver picks single-consumer or multi-consumer mode through the flags passed to `buf_ring_alloc()`, so the same primitive serves either shape depending on the subsystem's concurrency needs. For the full API surface, see Appendix B's `buf_ring(9)` entry.
- **`epoch(9)`** provides a read-mostly pattern where readers proceed without any synchronization and writers coordinate among themselves.
- **`mi_switch` fast paths** in the scheduler use atomic operations to avoid mutexes entirely in the common case.

Reading any of these is good study material, but they are specialised. For a beginner driver, the combination of `atomic` for single fields and `mutex` for composite state covers 99% of real drivers. We stay with those for Chapter 11 and let Chapter 12 and later chapters introduce the more sophisticated patterns when specific drivers need them.

### When to Reach for an Atomic (Decision Guide)

As a decision guide, use this flowchart when deciding whether to make a field atomic or lock it:

1. Is the field a single word (8, 16, 32, or 64 bits on a platform where that size is atomic)? If no, it must be locked.
2. Does the correctness of my code depend on this field being consistent with another field? If yes, it must be locked with the other field.
3. Does my operation on the field involve more than one read-modify-write sequence? If yes, either use `atomic_cmpset` in a loop or lock.
4. Does the code need to sleep while holding the state? If yes, use a mutex and `mtx_sleep`; atomics cannot sleep.
5. Otherwise, an atomic operation is probably the right tool.

For the `myfirst` driver, this flowchart points at atomics for the byte counters (now `counter(9)`), the "generation" counter (Section 4's load/unload counter), and informational flags. It points at the mutex for everything involving the cbuf.

### Wrapping Up Section 4

Atomic operations are a precise tool for a precise job: updating a single word-sized field without racing. They are fast, they compose with memory ordering primitives when needed, and they scale well. They do not replace mutexes for anything more complicated than a single field.

In the driver, we migrated the byte counters to `counter(9)` per-CPU counters, which removed them from the set of fields protected by `sc->mtx` and gave us better performance under high load. The cbuf, with its composite invariants, stays protected by `sc->mtx`. Section 5 returns to that mutex and explains, in full, what it is and how it works.



## Section 5: Introducing Locks (Mutexes)

This is the central section of the chapter. Everything up to here has been preparation; everything after this reinforces and extends. By the end of the section, you will understand what a mutex is, what kinds FreeBSD provides, what rules apply to each, why Chapter 10 chose the specific kind it used, and how to verify that your driver obeys the rules.

### What a Mutex Is

A **mutex** (short for "mutual exclusion") is a synchronization primitive that allows only one thread at a time to hold it. Threads that try to acquire a mutex while it is held wait until the holder releases it, then one of the waiters acquires it. The guarantee is that, between any two acquisitions of the same mutex, exactly one thread executed the code region between its acquire and its release.

That guarantee is what turns sequences of instructions into **critical sections**: regions of code whose execution is serialized across all threads that use the mutex. A reader who holds the mutex can read any number of fields and trust that no concurrent writer is producing torn values. A writer who holds the mutex can update any number of fields and trust that no concurrent reader is observing a half-updated state.

Mutexes do not prevent concurrency; they shape it. Multiple threads can execute outside the critical section at the same time. They can even be queued up to enter the critical section at the same time. What they cannot do is execute the critical section concurrently. The serialization is the property we buy; the price is the cost of acquiring and releasing, plus any wait time.

### FreeBSD's Two Mutex Shapes

FreeBSD distinguishes two fundamental shapes of mutex, because the kernel itself runs in two distinct sets of contexts and each context has different constraints.

**`MTX_DEF`** is the default mutex, often called a **sleep mutex**. When a thread tries to acquire one that is held, it is put to sleep (added to a sleep queue, with its state set to `TDS_SLEEPING`) until the holder releases the mutex. A sleep mutex may block for an arbitrarily long time, so the code using it must be in a context where sleeping is legal: ordinary process context, a kernel thread, or a callout marked as mpsafe. Most driver code runs in contexts where sleep mutexes are appropriate.

**`MTX_SPIN`** is a **spin mutex**. When a thread tries to acquire one that is held, it *spins*: it executes a tight loop of atomic checks until the holder releases. A spin mutex never sleeps. The reason spin mutexes exist is that some contexts (specifically, hardware interrupt handlers on certain systems) cannot sleep at all. A thread that cannot sleep cannot wait for a sleep mutex; it needs a primitive that makes progress without descheduling. Spin mutexes have additional rules: they disable interrupts on the CPU that acquires them, they must be held for very short durations, and code holding them cannot call any function that could sleep.

For the `myfirst` driver, `MTX_DEF` is the correct choice. Our handlers run in process context, on behalf of a user syscall. They do not run in interrupt context. Sleeping is legal. The mutex can be a sleep mutex, which is simpler and places fewer restrictions on what the critical section can do.

The `mtx_init` call from Chapter 10 specifies `MTX_DEF`:

```c
mtx_init(&sc->mtx, device_get_nameunit(dev), "myfirst", MTX_DEF);
```

That last argument is the lock type. If we had wanted a spin mutex, it would be `MTX_SPIN`, and the rest of the driver would have been more constrained.

### The Life Cycle of a Mutex

Every mutex has a life cycle: create, use, destroy. The functions are `mtx_init` and `mtx_destroy`:

```c
void mtx_init(struct mtx *m, const char *name, const char *type, int opts);
void mtx_destroy(struct mtx *m);
```

`mtx_init` initializes the internal fields of the mutex structure and registers the mutex with `WITNESS` (if enabled) so that lock-order checking can apply. `mtx_destroy` tears it down.

The four arguments to `mtx_init` are:

- `m`: the mutex structure (typically a field of a larger struct, not a separate allocation).
- `name`: a human-readable identifier that shows up in `ps`, `dmesg`, and `WITNESS` messages. For our driver this is `device_get_nameunit(dev)` (for instance, `"myfirst0"`).
- `type`: a short string classifying the mutex; `WITNESS` groups mutexes with the same `type` string as related. For our driver this is `"myfirst"`.
- `opts`: the flags, including `MTX_DEF` or `MTX_SPIN`, and optional flags like `MTX_RECURSE` (allow the same thread to acquire the mutex multiple times) or `MTX_NEW` (guarantee that the memory is uninitialized). For our driver, `MTX_DEF` alone is correct.

The `name` and `type` matter for observability. When a user runs `procstat -kk`, they see `myfrd` (the wait message from `mtx_sleep`) and `myfirst0` (the mutex name) in the process's wait information. When `WITNESS` flags a lock-order reversal, it names the mutex by its `name`. Good names make debugging concurrency problems dramatically easier.

### Acquiring and Releasing

The two fundamental operations are `mtx_lock` and `mtx_unlock`. Both take a pointer to the mutex:

```c
void mtx_lock(struct mtx *m);
void mtx_unlock(struct mtx *m);
```

`mtx_lock` acquires the mutex. If the mutex is not held, it acquires immediately (the cost is one atomic operation). If the mutex is held, the calling thread goes to sleep on the mutex's wait list. When the current holder releases, one waiter is woken and acquires the mutex.

`mtx_unlock` releases the mutex. If there are waiters, one is woken and scheduled. If there are no waiters, the unlock completes with only the cost of a store.

These two operations compose into the critical-section idiom:

```c
mtx_lock(&sc->mtx);
/* critical section: mutual exclusion is guaranteed here */
mtx_unlock(&sc->mtx);
```

Inside the critical section, the thread holding the mutex is the only thread that can be in any critical section protected by the same mutex. Other threads trying to enter the critical section are asleep, waiting.

### The Other Operations

Several other operations are occasionally useful:

**`mtx_trylock(&m)`**: attempts to acquire the mutex. Returns nonzero on success (mutex acquired) and zero on failure (mutex held by another thread). The calling thread never sleeps. This is useful when you want to do something only if the mutex is available, or when you want to avoid holding a lock across a potentially long operation.

**`mtx_assert(&m, what)`**: asserts that the mutex is or is not held, for debugging. The `what` argument is one of `MA_OWNED` (current thread holds the mutex), `MA_NOTOWNED` (current thread does not hold the mutex), `MA_OWNED | MA_RECURSED` (held, recursively), or `MA_OWNED | MA_NOTRECURSED` (held, not recursively). On an `INVARIANTS` kernel, the assertion fires as a panic if violated. On a non-`INVARIANTS` kernel, it does nothing. Use this liberally; it is free in production and catches bugs in development.

**`mtx_sleep(&chan, &mtx, pri, wmesg, timo)`**: the sleep primitive we used in Chapter 10. Atomically releases `mtx`, sleeps on `chan`, reacquires `mtx` before returning. The atomicity matters: the lock drop and the sleep cannot be interrupted by a concurrent `wakeup`.

**`mtx_initialized(&m)`**: returns nonzero if the mutex has been initialized. Useful in rare teardown paths where you want to check whether you need to call `mtx_destroy`.

### Re-Examining the Chapter 10 Design

With the vocabulary in place, let us re-read the Chapter 10 design choices and confirm they are correct.

**One mutex, covering all shared softc state.** The simplest possible design is one mutex protecting everything that is shared. This is what we have. It is the right starting point for any driver. It is almost never the wrong answer, even if a finer-grained design might perform better. Finer-grained locking has costs (lock ordering, understanding, bugs when new fields are added); a single mutex has the benefit of being obviously correct.

**`MTX_DEF` (sleep mutex).** The driver runs in process context. All the work is on behalf of user syscalls. There are no interrupt handlers. `MTX_DEF` is the correct choice.

**Lock acquired for every access to shared state.** Every read and write of the protected fields is inside a `mtx_lock` / `mtx_unlock` pair. The audit in Section 3 confirmed this.

**Lock released before calling functions that may sleep.** `uiomove(9)` may sleep (on a user-space page fault). `selwakeup(9)` may take its own locks and should not be called under our mutex. Our handlers drop the mutex before these calls. This is the rule from Chapter 10; we can now explain *why* it matters, which Section 5.6 does below.

**`mtx_sleep` uses the mutex as interlock.** The atomic drop-and-sleep operation is what prevents a race between the condition check and the sleep. If we did not use `mtx_sleep` and instead unlocked, then slept, a concurrent `wakeup` could fire in the window between unlock and sleep, and we would miss the wakeup and sleep forever. `mtx_sleep` exists precisely to close that window.

Every choice Chapter 10 made is now one we can defend from first principles. The mutex is not there by habit; it is there because every aspect of the design requires it.

### The Sleep-with-Mutex Rule

One rule recurs often enough to deserve its own treatment: **do not hold a non-sleepable lock across a sleeping operation**. For `MTX_DEF` mutexes, this translates to: do not hold the mutex across any call that may sleep unless you are using `mtx_sleep` itself (which atomically drops the mutex for the duration of the sleep).

Why does this matter?

First, sleeping with a mutex held blocks any other thread that needs the same mutex, for the entire duration of the sleep. If the sleep is long, throughput collapses; if the sleep is indefinite, the system deadlocks.

Second, sleeping in the kernel involves the scheduler, which may need its own locks. If those locks have a defined order with respect to your mutex, and your held mutex is further up the order, you may trigger a lock-order violation.

Third, on `WITNESS`-enabled kernels, sleeping with a mutex held raises a warning. On `INVARIANTS`-enabled kernels, certain specific cases of this rule (the well-known sleeping primitives) will panic.

The rule's scope is broader than it first appears. A call that "may sleep" includes:

- `malloc(9)` with `M_WAITOK`.
- `uiomove(9)`, `copyin(9)`, `copyout(9)` (each may fault on user memory and wait for the page to be paged in).
- Most functions in the `vfs(9)` layer.
- Most functions in the `file(9)` layer.
- `taskqueue_enqueue(9)` in some paths.
- Any function whose implementation chain includes any of the above.

The practical technique is to identify every function you call from inside a critical section and ask: "can this sleep?" If the answer is yes or you are not sure, drop the lock before the call. The Chapter 10 handlers follow this rule: they drop `sc->mtx` before every `uiomove`, and they drop it before every `selwakeup`.

### Priority Inversion and Priority Propagation

A subtle concurrency issue is **priority inversion**. Suppose thread L (low priority) acquires mutex M. Thread H (high priority) wants to acquire M and has to wait for L. In the meantime, thread M (medium priority, no relation to our mutex variable) is doing work unrelated to the mutex. The scheduler, seeing that H is blocked and M is runnable, will schedule M over L. L therefore makes no progress. M keeps L from running. H keeps waiting for L. A medium-priority thread has effectively blocked a high-priority thread, even though they share no resources.

This is priority inversion. It is a famous bug; the Mars Pathfinder mission briefly experienced a version of it in the 1990s.

The FreeBSD kernel handles priority inversion through **priority propagation** (also called priority inheritance). When a high-priority thread blocks on a mutex held by a lower-priority thread, the kernel temporarily raises the holder's priority to match the waiter's. L now runs at H's priority, so M cannot preempt it, and L finishes the critical section quickly. When L releases the mutex, its priority drops back, H acquires the mutex, and the inversion is resolved.

The practical consequence for a driver writer is that you mostly do not need to think about priority inversion. The kernel handles it for you on any `MTX_DEF` mutex. But the mechanism has costs: the holder of a contended mutex may run at a higher priority than it would otherwise, potentially for the duration of the critical section. That is another reason to keep critical sections short.

### Lock Order and Deadlocks

Priority inversion is a problem within a single mutex. **Deadlock** is a problem across multiple mutexes.

Consider two mutexes, A and B. Thread 1 acquires A, then wants to acquire B. Thread 2 acquires B, then wants to acquire A. Each thread holds what the other wants. Neither releases what it has until it acquires what it wants. Neither can make progress. The system has deadlocked.

The classical defence against deadlock is **lock ordering**: every thread acquires its locks in the same global order. If all threads that need both A and B always acquire A before B, deadlock of this kind is impossible. Thread 2 would acquire A before B, not the other way around; if it could not get A, it would wait for it; once it had A, it would then acquire B; it would not hold B and wait for A.

In FreeBSD, `WITNESS` enforces lock ordering. The first time the kernel observes a thread holding lock A and acquiring lock B, it records the order A-before-B as valid. If it later sees another thread (or even the same thread) hold B and try to acquire A, that is a lock-order reversal, and `WITNESS` prints a warning. If `INVARIANTS` is also enabled and the configuration demands it, the warning becomes a panic.

For the `myfirst` driver, we have only one mutex. There is no lock order to worry about (a mutex has a trivial order with itself: you either hold it or you don't, and `mtx_lock` with `MTX_RECURSE` unset will panic if a thread already holding the mutex tries to acquire it again). As the driver grows, if additional mutexes are introduced, a lock order must be defined and documented. Chapter 12 covers this in depth for the case of multiple lock classes (for example, a mutex for the control path and an `sx` lock for the data path).

### WITNESS: What It Catches

`WITNESS` is the kernel's lock-order and locking-discipline checker. It is enabled by `options WITNESS` in the kernel configuration; it is often paired with `options INVARIANTS` for maximum coverage.

What `WITNESS` catches:

- **Lock-order reversals**: thread acquires locks in an order not matching a previously-observed order.
- **Sleeping with a non-sleepable lock held**: thread calls `msleep`, `tsleep`, `cv_wait`, or any other sleeping primitive while holding an `MTX_SPIN` mutex.
- **Recursive acquisition of a non-recursive lock**: thread tries to acquire a mutex it already holds, and the mutex was not initialized with `MTX_RECURSE`.
- **Release of a lock not held**: thread calls `mtx_unlock` on a mutex it does not hold.
- **Sleeping with certain named sleep mutexes held**: more specifically, if `INVARIANTS` is also enabled, `mtx_assert(MA_NOTOWNED)` is checked before sleeping.

What `WITNESS` does not catch:

- **Missing locks**: if you forgot to take a lock, `WITNESS` has no way to know you should have.
- **Use-after-free of a mutex**: if you destroy a mutex and then use it, `WITNESS` may or may not catch it depending on how quickly the memory is reused.
- **Data races that do not involve locks**: two threads touching the same unlocked variable are invisible to `WITNESS`.

For this reason, `WITNESS` is a lint tool, not a proof. A driver can pass `WITNESS` and still be wrong. But a driver that fails `WITNESS` is almost certainly wrong, and the warnings are usually specific enough to point at the line.

Turn on `WITNESS` and `INVARIANTS` in your development kernel. Run the driver's test suite under the debug kernel. If warnings appear, investigate every one. This is the single most effective habit in driver debugging.

### Running the Driver Under WITNESS

If you have not already, build a debug kernel. On a FreeBSD 14.3 release system, the installed `GENERIC` kernel does not have `WITNESS` or `INVARIANTS` enabled, because those options carry a runtime cost that is undesirable in production. You have to build a kernel that does. The simplest way is to copy `GENERIC` and add the relevant options. The lines you need are:

```text
options         INVARIANTS
options         INVARIANT_SUPPORT
options         WITNESS
options         WITNESS_SKIPSPIN
```

Section "Building and Booting a Debug Kernel" later in this chapter walks through the full build, install, and reboot procedure step by step. The short version is:

```sh
# cd /usr/src
# make buildkernel KERNCONF=MYFIRSTDEBUG
# make installkernel KERNCONF=MYFIRSTDEBUG
# shutdown -r now
```

where `MYFIRSTDEBUG` is the name of the kernel configuration file you created.

After rebooting, load the driver and run a test. If `WITNESS` fires, `dmesg` will show something like:

```text
lock order reversal:
 1st 0xfffffe00020b8a30 myfirst0 (myfirst, sleep mutex) @ ...:<line>
 2nd 0xfffffe00020b8a38 foo_lock (foo, sleep mutex) @ ...:<line>
lock order foo -> myfirst established at ...
```

The warning names both mutexes, their addresses, their types, and the source locations involved. From here, you trace the code and fix the order.

For the Chapter 10 Stage 4 driver, `WITNESS` should be silent: we have one mutex, used consistently, and no sleep-with-mutex violations. If you see warnings, that is a bug worth investigating.

### When a Mutex Is the Wrong Answer

Three situations call for something other than a plain `MTX_DEF` mutex. Chapter 12 covers each in depth; we mention them here so the reader knows the landscape.

**Many readers, few writers.** If the critical section is mostly read-only, with occasional writes, a mutex serializes readers unnecessarily. A reader/writer lock (`sx(9)` or `rw(9)`) allows many readers to hold the lock simultaneously and serializes only writers. The cost is higher overhead per acquire/release and more complexity in the rules; the benefit is scalability on read-heavy workloads.

**Blocking wait on a condition, not on a lock.** When a thread needs to wait until a specific condition becomes true (for example, "data is available in the cbuf"), `mtx_sleep` with a channel is one way to express it. A named **condition variable** (`cv(9)`) is another way, often cleaner and more explicit. Chapter 12 covers `cv_wait`, `cv_signal`, and `cv_broadcast`.

**Extremely short operations on a single field.** Atomics, as we saw in Section 4. No need for a mutex at all.

For `myfirst`, none of these apply yet. The critical sections are short, the operations involve composite invariants, and neither read-heavy nor condition-variable patterns fit better than the mutex we have. Chapter 12 will introduce the alternatives when the driver evolves to a point where they are justified.

### A Mini-Walkthrough: Tracing a Lock

To make the mechanics concrete, let us trace what happens when two threads race for the mutex.

Thread A calls `myfirst_read`. It reaches `mtx_lock(&sc->mtx)`. The mutex is not held (the initial state is "owner = NULL, waiters = none"). `mtx_lock` executes an atomic compare-and-swap: "if owner is NULL, set owner to curthread." It succeeds. A is now in the critical section.

Thread B calls `myfirst_read` on a different CPU. It reaches `mtx_lock(&sc->mtx)`. The mutex is held (owner = A). The compare-and-swap fails. `mtx_lock` now has to arrange for B to wait.

B enters the slow path. It adds itself to the mutex's wait list. It sets its thread state to blocked. It calls the scheduler, which picks some other runnable thread (possibly none, in which case the CPU idles).

Time passes. A finishes its critical section and calls `mtx_unlock(&sc->mtx)`. `mtx_unlock` sees that there are waiters. It picks one (usually the highest-priority waiter, with FIFO among equal priorities) and wakes it. That waiter, probably B, is made runnable.

The scheduler sees B runnable and schedules it. B resumes inside `mtx_lock`'s slow path. `mtx_lock` now records that the mutex is held by B and returns. B is in the critical section.

Between A's `mtx_unlock` and B's `mtx_lock` returning, the mutex was unowned for a brief moment. No other thread could have slipped in, because the unlock and the wakeup are arranged so that whoever wakes up next is the next owner. This is one of the things `mtx_lock` does that a hand-rolled "check a flag, sleep, check again" implementation would not get right.

All of this is happening inside `/usr/src/sys/kern/kern_mutex.c`. If you open that file and look for `__mtx_lock_sleep`, you can see the slow path's code. It is more elaborate than the sketch above; it handles priority propagation, adaptive spinning, and several corner cases. The core idea, though, is what the sketch describes.

### Reading a Lock Contention Story

When a driver performs poorly under load, one of the first things to check is whether the mutex is contended: how often threads have to wait for it and for how long. FreeBSD provides this information through the `debug.lock.prof.*` sysctl tree, which is enabled by the `LOCK_PROFILING` kernel option, and through the `lockstat(1)` user-space tool, which is part of the DTrace toolkit.

We will not build a full performance-analysis story in this chapter; that is Chapter 12 and Part 4 material. But if you are curious, on a kernel built with `options LOCK_PROFILING`, try:

```sh
# sysctl debug.lock.prof.enable=1
# ./producer_consumer
# sysctl debug.lock.prof.enable=0
# sysctl debug.lock.prof.stats
```

The output lists every lock the kernel has seen, the maximum and total wait time, the maximum and total hold time, the acquisition count, and the source file and line number where the lock was last touched. For our driver, `myfirst0` should appear with modest numbers, because the critical sections are short. If the numbers were large, we would have a signal that the mutex is contended and a finer-grained design might help. For Chapter 11's purposes, we are not optimizing; we are ensuring correctness.

### Applying What We Have Learned

Let us consolidate. In the driver as of Chapter 10 Stage 4, the mutex `sc->mtx` is:

- An `MTX_DEF` sleep mutex, created in `myfirst_attach` and destroyed in `myfirst_detach`.
- Named `"myfirst0"` (and similar for other unit numbers), typed `"myfirst"`.
- Held by the I/O handlers around every access to the cbuf, the byte counters, and the other protected fields.
- Released before every call that may sleep (`uiomove`, `selwakeup`).
- Used as the interlock argument to `mtx_sleep`.
- Asserted as held inside the helper functions via `mtx_assert(MA_OWNED)`.
- Documented in the locking comment at the top of the file and in `LOCKING.md`.

With the concepts in Section 4 and Section 5, every one of those properties is defensible from first principles. The mutex is not ceremony. It is the infrastructure that turns a driver that happens to work into a driver that is correct.

### Spin Mutexes in More Detail

We have said that spin mutexes (`MTX_SPIN`) exist because some contexts cannot sleep. Let us look more closely at why.

The FreeBSD kernel has several execution contexts. Most driver code runs in **thread context**: the kernel is executing on behalf of some thread (a user thread that made a syscall, a kernel thread dedicated to some task, or a callout running in mpsafe mode). In thread context, sleeping is legal: the scheduler can park the thread and schedule another one.

A small but critical set of contexts cannot sleep. The most important is **hardware interrupt context**: the code that runs when a hardware interrupt fires. An interrupt may preempt any thread at any instant, run a short handler (called an ithread or filter), and return. While the handler runs, the thread it preempted cannot make progress. The handler must finish quickly, and it must not block. Sleeping would mean calling the scheduler, and calling the scheduler from inside an interrupt is not safe on the platforms FreeBSD supports.

Another non-sleep context is **critical sections** entered with `critical_enter(9)`. These disable preemption on the current CPU; the code inside runs to completion without the scheduler being allowed to pick a different thread. Critical sections are rarely used by driver writers directly; they appear more in low-level kernel code.

For code in any non-sleep context, a sleep mutex is the wrong tool. Acquiring a sleep mutex that is held would require sleeping, and you cannot. You need a mutex that spins: tries in a tight loop until it succeeds.

`MTX_SPIN` mutexes do exactly that. When you call `mtx_lock_spin(&m)`, the code:

1. Disables interrupts on the current CPU (because otherwise an interrupt handler could preempt you while you hold the spin mutex, leading to deadlock if the handler wants the same mutex).
2. Tries an atomic compare-and-swap to acquire the mutex.
3. If it fails, spins in a loop, periodically retrying.
4. Once acquired, proceeds with the critical section. Interrupts remain disabled.
5. `mtx_unlock_spin(&m)` releases the mutex and re-enables interrupts.

The rules for spin mutexes are strict:

- The critical section must be **very short**: holding a spin mutex is holding back every other CPU that wants it plus disabling interrupts on the current CPU. Microseconds matter.
- You **cannot sleep** while holding a spin mutex. `malloc(9)` with `M_WAITOK` is illegal. `mtx_sleep(9)` is illegal. Even a page fault is illegal (you must not touch user memory or paged kernel memory under a spin mutex).
- You **cannot acquire a sleep mutex** while holding a spin mutex. The sleep mutex would try to sleep if contended, and you cannot sleep.

For the `myfirst` driver, spin mutexes are the wrong choice. We never run in interrupt context. Our critical sections may call into the cbuf helpers, which contain memcpy loops that are short but not microsecond-tiny. `MTX_DEF` is correct.

For a driver that *does* run in interrupt context (Chapter 14 will cover this), spin mutexes often appear on the shortest critical sections: the ones between the interrupt handler and the top-half code. Longer critical sections can be protected by an `MTX_DEF` mutex that the interrupt handler does not take; the handler simply queues work for the top-half to do under its sleep mutex.

### The MTX_RECURSE Flag and Lock Recursion

A subtle flag in `mtx_init` is `MTX_RECURSE`. Without it, a thread that already holds a mutex and tries to acquire it again will panic (under `INVARIANTS`) or deadlock (without `INVARIANTS`). With `MTX_RECURSE`, the second acquisition is counted; the mutex is released only when every acquire has been matched with an unlock.

Most drivers do not need `MTX_RECURSE`. The fact that a function tries to acquire a lock it already holds usually means the code is structured badly: a helper is being called from both lock-holding and non-lock-holding contexts, and the helper does not know which it is.

Fix the structure, not the mutex. Split the helper into a "with lock" and a "without lock" version. Name them `_locked` and `_unlocked` respectively, matching FreeBSD convention. Example:

```c
static size_t
myfirst_buf_read_locked(struct myfirst_softc *sc, void *dst, size_t n)
{
        mtx_assert(&sc->mtx, MA_OWNED);
        /* ... buffer logic ... */
}

static size_t
myfirst_buf_read(struct myfirst_softc *sc, void *dst, size_t n)
{
        size_t got;

        mtx_lock(&sc->mtx);
        got = myfirst_buf_read_locked(sc, dst, n);
        mtx_unlock(&sc->mtx);
        return (got);
}
```

Now the two call sites are explicit: one takes the lock, the other does not. Neither needs `MTX_RECURSE`. Neither confuses the reader. This is how you should structure code as your driver grows.

There are rare exceptions where `MTX_RECURSE` is legitimately useful. A complex data structure with internal recursion (for example, a tree that uses the same lock at every node) may need it. `buf_ring`'s drain operation uses it. Those are specialised cases; for the ordinary driver, structure your code to avoid recursion and do not add the flag.

### Adaptive Spinning

FreeBSD's sleep mutexes include an optimization called **adaptive spinning**. When `mtx_lock` cannot acquire an `MTX_DEF` mutex because another CPU holds it, the lock does not immediately put the thread to sleep. It first spins for a short time, hoping that the holder will release the mutex quickly. Only if the spin exceeds a threshold does the lock fall through to the sleep path.

The rationale: most mutex-held intervals are short (microseconds), and sleeping plus waking is expensive. Spinning for a few microseconds usually beats going through the scheduler. Adaptive spinning recovers most of the performance benefit of spin mutexes without the constraints.

You can see the implementation in `/usr/src/sys/kern/kern_mutex.c`, in the function `__mtx_lock_sleep`. The code checks whether the holder is currently running on another CPU and spins as long as it is. If the holder descheduled (went to sleep itself), spinning is pointless and the waiter goes to sleep too.

For the driver writer, adaptive spinning means the mutex performance is better than a naive analysis would suggest. A short critical section on a low-contention mutex costs roughly the atomic compare-and-swap, nothing more. Only under real contention do you pay the full sleep/wake cost.

### A Closer Look at mtx_sleep's Atomicity

Chapter 10 explained that `mtx_sleep` atomically drops the mutex and puts the caller on the sleep queue. The atomicity matters because the alternative, drop-then-sleep, has a window through which a `wakeup` can escape unheard.

Consider the alternative sequence:

```c
mtx_unlock(&sc->mtx);
sleep_on(&sc->cb);
mtx_lock(&sc->mtx);
```

Between the unlock and the sleep, another CPU could acquire the mutex, observe that the condition we were waiting for has become true, call `wakeup(&sc->cb)`, and return. Our wakeup has been delivered to a sleep queue we have not yet joined. We sleep on a condition that, from our thread's perspective, will never again become true: the signal has already been missed.

This is the classical **lost wakeup** race. It is one of the core reasons the atomic drop-and-sleep operation exists. `mtx_sleep` closes the window by enqueueing the caller on the sleep queue *before* it drops the outer mutex. The "Reference: A Closer Look at Sleep Queues" section later in this chapter walks through the sequence of locks and state transitions in more detail for readers who want to see exactly how the kernel arranges this. For the body of the chapter, the rule is enough: use `mtx_sleep` with the outer mutex as its interlock, and the lost-wakeup race disappears.

### A Guided Tour Through kern_mutex.c

If you have an hour of patience, opening `/usr/src/sys/kern/kern_mutex.c` is worth the investment. You do not need to understand every line. Three functions are particularly illuminating:

**`__mtx_lock_flags`**: the fast path for acquiring a mutex. The interesting thing is how short it is. In the uncontested case, acquiring a mutex is essentially one atomic compare-and-swap plus some lock-profiling bookkeeping. That is all.

**`__mtx_lock_sleep`**: the slow path, reached only when the fast path's compare-and-swap fails. This is where adaptive spinning, priority propagation, and the actual sleep queue work happen. The code is elaborate, but the structure is: try a few spins, hand off to the sleep queue, re-enter the scheduler, eventually acquire.

**`__mtx_unlock_flags`** and **`__mtx_unlock_sleep`**: the release paths. Also mostly fast: atomic release, then, if waiters exist, wake one up.

You are not expected to read every line. You are expected to be able to say: "this is where the atomic I use actually lives, and this is what it does". Twenty minutes of skimming gets you that.

### Comparing Mutex to Semaphore, Monitor, and Binary Flag

For readers who have encountered other synchronization primitives, it helps to situate FreeBSD's mutex.

A **semaphore** is a counter. Threads "P" (decrement) and "V" (increment) it; a P that would go negative blocks. A binary semaphore (counter values 0 or 1) is similar to a mutex, but semaphores typically allow the "release" to be done by a thread other than the one that acquired. Mutexes require the same thread to release.

A **monitor** is a language-level construct combining a mutex and one or more condition variables, with the mutex automatically acquired on entry and released on exit. C does not have monitors as a language feature, but the pattern of "mutex + condition variable" is the same idea.

A **binary flag** (a `volatile int` that threads set and clear) is what naive programmers sometimes use to implement mutual exclusion. It does not work: two threads can both see the flag as zero and both set it to one, both proceed as if they were exclusive. This is the race we saw with the counter in Section 2. Real mutexes use atomic compare-and-swap, not naked flags.

FreeBSD's mutex is a traditional mutex: only the acquiring thread may release, recursive acquisition requires explicit opt-in, the primitive integrates with the scheduler for blocking, and priority propagation is built in. It is the simplest and most commonly appropriate tool for driver synchronization.

### Mutex Lifetime Rules

Every mutex has a lifetime. The rules are:

1. Call `mtx_init` exactly once before any use.
2. Acquire and release the mutex any number of times.
3. Call `mtx_destroy` exactly once after all use. Calling `mtx_destroy` while any thread is blocked on the mutex is undefined.
4. After `mtx_destroy`, the memory may be reused. Do not access the mutex again.

The third rule is why our detach path is careful about the order: destroy the cdev (which stops new handlers from starting and waits for running ones to finish), then destroy the mutex. If we destroyed the mutex first, running handlers could still be inside `mtx_lock` or `mtx_unlock` on an already-destroyed mutex.

Lifetime bugs with mutexes tend to be catastrophic (memory corruption, use-after-free panics). They are easier to avoid than to debug, so the standard advice applies: always think carefully about the order of teardown.

### The MTX_NEW Option

`mtx_init`'s last argument can include `MTX_NEW`, which tells the function "this memory is fresh; you do not need to check for a prior initialization". On an `INVARIANTS` kernel, `mtx_init` verifies that the mutex was not previously initialized without a matching `mtx_destroy`. `MTX_NEW` skips this check.

Use `MTX_NEW` when you are initializing a mutex in memory that you know has not been used for this purpose before. Use the default (no `MTX_NEW`) when the mutex might be re-initialized (for example, in a handler that attaches and detaches repeatedly on the same softc). For our driver, the softc is re-allocated on each attach, so the mutex memory is always fresh; `MTX_NEW` is harmless but not required.

### Nested Locks and Deadlock Patterns

A thread that holds lock A and wants to acquire lock B is performing a **nested lock acquisition**. Nested acquisitions are where deadlocks and lock-order violations live. Four patterns cover nearly all of them.

**Pattern 1: Simple two-lock deadlock.** Thread 1 holds A, wants B. Thread 2 holds B, wants A. Neither makes progress. Fix: globally order the locks so every thread acquires A before B.

**Pattern 2: Lock order across subsystems.** Subsystem X always acquires its lock first; subsystem Y always acquires its lock first. If they ever need each other's locks, the order depends on which of X and Y happens to call into the other. Fix: document a subsystem ordering that is consistent across every cross-subsystem path.

**Pattern 3: Lock inversion via callback.** A function is called with lock A held. Inside, it calls a callback, which tries to acquire A again. If A is not recursive, deadlock. Fix: drop A before the callback, or split the function so the callback is invoked outside the lock.

**Pattern 4: Sleep-with-lock deadlock.** A function holds a mutex and then calls something that sleeps. If the sleeper needs the same mutex (perhaps through a different code path), deadlock. Fix: drop the mutex before sleeping, or use `mtx_sleep` so the drop happens atomically.

For the `myfirst` driver, we have one lock and no nested acquisitions; none of the patterns apply. Chapter 12 introduces `sx` locks; as soon as we have a second lock class, patterns 1 and 3 become relevant, and `WITNESS` becomes critical.

### Wrapping Up Section 5

A mutex is a tool for serializing access to shared state. FreeBSD provides two shapes (`MTX_DEF` for sleep mutexes, `MTX_SPIN` for spin mutexes), and `MTX_DEF` is the correct choice for our driver. The API is small: `mtx_init`, `mtx_lock`, `mtx_unlock`, `mtx_destroy`, `mtx_assert`, `mtx_sleep`. The rules are precise: hold short, do not sleep while holding a non-sleepable lock, acquire locks in a consistent order, release what you acquire, and use `WITNESS` to check.

Section 6 takes the theory we have built and exercises it: how to write user-space programs that actually stress the driver, and how to observe the results.



## Section 6: Testing Multi-threaded Access

Building a correct driver is half the work. The other half is developing tests that can catch the mistakes that slip through the design review. Chapter 10 introduced `producer_consumer.c`, a two-process round-trip test that is the single best test in this book's kit for detecting buffer-level correctness problems. Chapter 11 takes that foundation and adds the tools you need for concurrency specifically: multi-threaded tests using `pthread(3)`, multi-process tests using `fork(2)`, and a load harness that can sustain pressure on the driver for long enough to expose rare timing bugs.

The goal is not to exhaustively test every possible interleaving. That is impossible. The goal is to increase the rate at which the scheduler visits interleavings the driver has not seen, so that, if a bug is present, we are likely to observe its effects.

### The Testing Ladder

Concurrency testing climbs a ladder. At the bottom is a single-threaded test: one process, one thread, one call at a time. This is what `cat` and `echo` do. At the top is a distributed multi-process, multi-threaded, timing-varying, long-running test. The higher you climb, the more likely you are to catch concurrency bugs; the more expensive each test becomes to set up and interpret.

The rungs, from bottom to top:

1. **Single-threaded smoke tests.** One `cat`, one `echo`. Useful for confirming the driver is alive; useless for concurrency.
2. **Two-process round trip.** `producer_consumer` from Chapter 10. One writer, one reader, content verification. Catches most single-lock issues.
3. **Multi-threaded within a process.** `pthread`-based tests where multiple threads in the same process talk to the same descriptor or to distinct descriptors. Exposes intra-process concurrency.
4. **Many-process stress.** `fork`-based tests with N producers and M consumers. Exposes inter-process concurrency and lock contention.
5. **Long-running stress.** Any of the above, run for hours. Exposes timing-sensitive bugs that are rare on the per-operation basis but certain given enough time.

We will build rungs 3, 4, and 5 in this section. Rung 2 already exists from Chapter 10.

### Multiple Threads in One Process

A single process with multiple threads is useful because the threads share file descriptors. Two threads can read from the same descriptor; both calls go into `myfirst_read` with the same `dev` and (critically) the same per-descriptor `fh`.

Here is a minimal multi-threaded reader:

```c
/* mt_reader.c: multiple threads reading from one descriptor. */
#include <sys/types.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DEVPATH         "/dev/myfirst"
#define NTHREADS        4
#define BYTES_PER_THR   (256 * 1024)
#define BLOCK           4096

static int      g_fd;
static uint64_t total[NTHREADS];
static uint32_t sum[NTHREADS];

static uint32_t
checksum(const char *p, size_t n)
{
        uint32_t s = 0;
        for (size_t i = 0; i < n; i++)
                s = s * 31u + (uint8_t)p[i];
        return (s);
}

static void *
reader(void *arg)
{
        int tid = *(int *)arg;
        char buf[BLOCK];
        uint64_t got = 0;
        uint32_t sm = 0;

        while (got < BYTES_PER_THR) {
                ssize_t n = read(g_fd, buf, sizeof(buf));
                if (n < 0) {
                        if (errno == EINTR)
                                continue;
                        warn("thread %d: read", tid);
                        break;
                }
                if (n == 0)
                        break;
                sm += checksum(buf, n);
                got += n;
        }
        total[tid] = got;
        sum[tid] = sm;
        return (NULL);
}

int
main(void)
{
        pthread_t tids[NTHREADS];
        int ids[NTHREADS];

        g_fd = open(DEVPATH, O_RDONLY);
        if (g_fd < 0)
                err(1, "open %s", DEVPATH);

        for (int i = 0; i < NTHREADS; i++) {
                ids[i] = i;
                if (pthread_create(&tids[i], NULL, reader, &ids[i]) != 0)
                        err(1, "pthread_create");
        }
        for (int i = 0; i < NTHREADS; i++)
                pthread_join(tids[i], NULL);

        uint64_t grand = 0;
        for (int i = 0; i < NTHREADS; i++) {
                printf("thread %d: %" PRIu64 " bytes, checksum 0x%08x\n",
                    i, total[i], sum[i]);
                grand += total[i];
        }
        printf("grand total: %" PRIu64 "\n", grand);

        close(g_fd);
        return (0);
}
```

The companion file under `examples/part-03/ch11-concurrency/userland/mt_reader.c` is identical to this listing; you can either type it from the book or copy it from the examples tree.

Build with:

```sh
$ cc -Wall -Wextra -pthread -o mt_reader mt_reader.c
```

Start a writer in another terminal (or fork one before creating threads), then run this tester. Each thread pulls bytes out of the driver. Because the driver has one mutex, the reads serialize; there is no concurrency benefit, but there is also no incorrectness. Each thread sees a subset of the stream, and the concatenation of all threads' bytes is the full stream.

This is an important property. The driver makes no promise about which thread sees which bytes; it only promises that the total is conserved. If you want per-reader streams, you need a different driver design (Challenge exercise 3 in Chapter 10 explored this). For now, the test confirms that multiple readers in one process behave correctly.

### Many Processes in Parallel

A `fork`-based test spawns N children, each doing its own thing against the device. This gives us independent processes, independent file descriptors, and independent schedule decisions. The kernel is more likely to interleave them in novel ways.

Here is the skeleton:

```c
/* mp_stress.c: N processes hammering the driver concurrently. */
#include <sys/types.h>
#include <sys/wait.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DEVPATH         "/dev/myfirst"
#define NWRITERS        2
#define NREADERS        2
#define SECONDS         30

static volatile sig_atomic_t stop;

static void
sigalrm(int s __unused)
{
        stop = 1;
}

static int
child_writer(int id)
{
        int fd;
        char buf[1024];
        unsigned long long written = 0;

        fd = open(DEVPATH, O_WRONLY);
        if (fd < 0)
                err(1, "writer %d: open", id);
        memset(buf, 'a' + id, sizeof(buf));

        while (!stop) {
                ssize_t n = write(fd, buf, sizeof(buf));
                if (n < 0) {
                        if (errno == EINTR)
                                continue;
                        break;
                }
                written += n;
        }
        close(fd);
        printf("writer %d: %llu bytes\n", id, written);
        return (0);
}

static int
child_reader(int id)
{
        int fd;
        char buf[1024];
        unsigned long long got = 0;

        fd = open(DEVPATH, O_RDONLY);
        if (fd < 0)
                err(1, "reader %d: open", id);

        while (!stop) {
                ssize_t n = read(fd, buf, sizeof(buf));
                if (n < 0) {
                        if (errno == EINTR)
                                continue;
                        break;
                }
                got += n;
        }
        close(fd);
        printf("reader %d: %llu bytes\n", id, got);
        return (0);
}

int
main(void)
{
        pid_t pids[NWRITERS + NREADERS];
        int n = 0;

        signal(SIGALRM, sigalrm);

        for (int i = 0; i < NWRITERS; i++) {
                pid_t pid = fork();
                if (pid < 0)
                        err(1, "fork");
                if (pid == 0) {
                        signal(SIGALRM, sigalrm);
                        alarm(SECONDS);
                        _exit(child_writer(i));
                }
                pids[n++] = pid;
        }
        for (int i = 0; i < NREADERS; i++) {
                pid_t pid = fork();
                if (pid < 0)
                        err(1, "fork");
                if (pid == 0) {
                        signal(SIGALRM, sigalrm);
                        alarm(SECONDS);
                        _exit(child_reader(i));
                }
                pids[n++] = pid;
        }

        for (int i = 0; i < n; i++) {
                int status;
                waitpid(pids[i], &status, 0);
        }
        return (0);
}
```

The companion source under `examples/part-03/ch11-concurrency/userland/mp_stress.c` matches this listing. The `signal(SIGALRM, sigalrm)` reinstall after `fork(2)` is intentional; `fork(2)` inherits the parent's signal disposition, but reinstalling makes the intent explicit and survives the (rare) case where the parent's handler had been changed in between.

Build and run:

```sh
$ cc -Wall -Wextra -o mp_stress mp_stress.c
$ ./mp_stress
writer 0: 47382528 bytes
writer 1: 48242688 bytes
reader 0: 47669248 bytes
reader 1: 47956992 bytes
```

Look at the totals. The sum of readers should equal the sum of writers (plus or minus whatever is left in the buffer at the end of the test window). If they do not, we have either a driver bug or a reporting bug; neither is acceptable.

This test, run for thirty seconds, produces roughly one hundred million bytes of traffic through the driver, with four concurrent processes. On a four-core machine, all four can be making progress at any instant. If there is a concurrency bug that our review missed, it has thirty seconds of real compute to find it.

### Extended-Duration Tests

For the most elusive bugs, run the tests for hours. A simple wrapper:

```sh
$ for i in $(seq 1 100); do
      echo "iteration $i" >> /tmp/mp_stress.log
      ./mp_stress >> /tmp/mp_stress.log 2>&1
      sleep 1
  done
```

After fifty iterations (roughly twenty-five minutes of cumulative driver stress), review the log. If every iteration's byte counts are internally consistent, you have a strong signal that the driver is not suffering from frequent concurrency bugs. If any iteration shows inconsistency, you have a bug; save the log, reproduce with lower `NWRITERS`/`NREADERS`, and investigate.

A script like the above is a cheap substitute for a proper continuous-integration pipeline. For serious driver work, a CI that runs the stress suite nightly on multiple hardware configurations is the gold standard. For learning, the script above is enough.

### Observing Without Disturbing

A subtle issue: adding logging to a concurrent test can itself disturb the timing and mask the bug. If your test prints a line for every operation, the printing becomes a bottleneck, the threads serialize around stdout, and the interesting interleavings no longer happen.

Techniques that reduce observer effect:

- **Buffer logs in memory; flush at the end.** Each thread appends to its own local array; `main` prints the arrays after all threads finish.
- **Use `ktrace(1)` instead of in-process printing.** `ktrace` captures syscalls from a running process without modifying the process; the dump can be analyzed afterward.
- **Use `dtrace(1)` probes.** `dtrace` is designed to have minimal impact on the observed code path.
- **Keep counters, not line-by-line logs.** A mismatch count is a single integer; it compresses a lot of information into something cheap to update.

`producer_consumer` from Chapter 10 uses the counter approach: it updates a checksum per block and a total byte count, then reports both at the end. The test is effectively invisible to the driver's timing.

### A Testing Workflow

Putting it together, here is a reasonable workflow for a new change to the driver:

1. **Smoke test.** Load the driver, `printf 'hello' > /dev/myfirst`, `cat /dev/myfirst`. Confirm basic operation.
2. **Round-trip test.** Run `producer_consumer`. Confirm zero mismatches.
3. **Multi-threaded in-process.** Run `mt_reader` against a continuously-running `cat /dev/zero > /dev/myfirst`. Confirm the total matches.
4. **Multi-process stress.** Run `mp_stress` for thirty seconds. Confirm the byte counts are consistent.
5. **Long-running stress.** Run the loop wrapper for thirty minutes to an hour.
6. **Debug-kernel regression.** Repeat steps 1-4 on a `WITNESS`-enabled kernel. Confirm no warnings.

If every step passes, the change is likely to be safe. If any step fails, the failure mode tells you where to look.

### A Latency-Measuring Tester

Sometimes what you want to know is not "does the driver work" but "how quickly does it respond under load". The mutex's cost, the sleep queue's wake-up latency, and the scheduler's decisions all combine to produce a distribution of response times that is worth observing directly.

Here is a simple latency tester. It opens the device, measures how long each `read(2)` takes, and prints a histogram.

```c
/* lat_tester.c: measure read latency against /dev/myfirst. */
#include <sys/types.h>
#include <sys/time.h>
#include <err.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DEVPATH "/dev/myfirst"
#define NSAMPLES 10000
#define BLOCK 1024

static uint64_t
nanos(void)
{
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return ((uint64_t)ts.tv_sec * 1000000000 + ts.tv_nsec);
}

int
main(void)
{
        int fd = open(DEVPATH, O_RDONLY | O_NONBLOCK);
        if (fd < 0)
                err(1, "open");
        char buf[BLOCK];
        uint64_t samples[NSAMPLES];
        int nvalid = 0;

        for (int i = 0; i < NSAMPLES; i++) {
                uint64_t t0 = nanos();
                ssize_t n = read(fd, buf, sizeof(buf));
                uint64_t t1 = nanos();
                if (n > 0)
                        samples[nvalid++] = t1 - t0;
                else
                        usleep(100);
        }
        close(fd);

        /* Simple bucketed histogram. */
        uint64_t buckets[10] = {0};
        const char *labels[10] = {
                "<1us   ", "<10us  ", "<100us ", "<1ms   ",
                "<10ms  ", "<100ms ", "<1s    ", ">=1s   ",
                "", ""
        };
        for (int i = 0; i < nvalid; i++) {
                uint64_t us = samples[i];
                int b = 0;
                if (us < 1000) b = 0;
                else if (us < 10000) b = 1;
                else if (us < 100000) b = 2;
                else if (us < 1000000) b = 3;
                else if (us < 10000000) b = 4;
                else if (us < 100000000) b = 5;
                else if (us < 1000000000) b = 6;
                else b = 7;
                buckets[b]++;
        }

        printf("Latency histogram (%d samples):\n", nvalid);
        for (int i = 0; i < 8; i++)
                printf("  %s %6llu\n",
                    labels[i], (unsigned long long)buckets[i]);
        return (0);
}
```

Run with a concurrent writer:

```sh
$ dd if=/dev/zero of=/dev/myfirst bs=1k &
$ ./lat_tester
Latency histogram (10000 samples):
  <1us       3421
  <10us      6124
  <100us      423
  <1ms         28
  <10ms         4
  <100ms        0
  <1s           0
  >=1s          0
```

Most reads complete in a microsecond or two. A handful take longer, usually because the reader had to wait on the mutex while the writer was inside. Very occasionally, a read takes milliseconds; that is usually because the thread was preempted or had to sleep.

This is the distribution of your driver's responsiveness under load. If the long tail is unacceptably long, the driver needs finer-grained locking or fewer critical-section operations. For our `myfirst`, the tail is short enough that nothing needs changing.

### A Tester That Probes for Torn Reads

If your driver has any unprotected multi-field reads (the book's recommendation is never, but real drivers sometimes have them), you can detect torn reads with a targeted tester.

Imagine we had a field that could be torn on a 32-bit platform. The tester would:

1. Fork a writer that continuously updates the field with known patterns.
2. Fork a reader that continuously reads the field and checks the pattern.
3. Report every read that does not match any valid pattern.

For our driver, no such field exists (all `uint64_t` counters are per-CPU, all composite state is under the mutex). But building the tester is a valuable exercise, because it trains you to write probes for specific classes of bug.

### Multi-Core Scaling

A test we have not yet written: does the driver scale as you add more cores?

The idea is simple: run `mp_stress` with increasing `NWRITERS`/`NREADERS` and observe the total throughput. Ideally, throughput should grow linearly with core count up to some saturation point. In practice, single-mutex drivers saturate early, because every operation serializes on the one mutex.

Build a scaling test:

```sh
$ for n in 1 2 4 8 16; do
    NWRITERS=$n NREADERS=$n ./mp_stress > /tmp/scale_$n.txt
    total=$(grep ^writer /tmp/scale_$n.txt | awk '{s+=$3} END {print s}')
    echo "$n writers, $n readers: $total bytes"
  done
```

The `mp_stress` program needs to accept `NWRITERS` and `NREADERS` from environment variables for this to work; exercise for the reader.

On a four-core machine, a single-mutex driver typically saturates around 2-4 writers plus 2-4 readers. Beyond that, throughput flattens or drops, because the mutex becomes the bottleneck. This is the signal that finer-grained locking (Chapter 12's `sx` locks, for example) might be justified. For `myfirst` at this stage, saturation is not a concern; the teaching pedagogy matters more than absolute throughput.

### Coverage of the Failure Modes

A final check: does the test kit cover every failure mode we enumerated in Section 1?

- **Data corruption**: `producer_consumer` catches this directly by comparing checksums.
- **Lost updates**: if a counter is wrong, the producer_consumer total will not match.
- **Torn values**: not directly tested, but any torn read that affects correctness propagates to a wrong checksum.
- **Inconsistent composite state**: if the cbuf indices become inconsistent, the driver will either corrupt data (caught by checksum) or deadlock (caught by the test hanging and our timeout).

The test kit is comprehensive against the failure modes we know about. It is not comprehensive against unknown bugs, but no test kit can be. The combination of debug-kernel runs, regression tests, and long-running stress tests is as close as we can get.

### Wrapping Up Section 6

Testing is the fastest-feedback part of driver work. The tests we have just built cover three orders of magnitude more interleavings than a single `cat` could. Running them on both a production and a debug kernel gives you verification from both angles.

Section 7 takes the next step: what do you do when a test fails?



## Section 7: Debugging and Verifying Thread Safety

A concurrency bug's most reliable signature is that it does not reproduce on demand. You run the test; it fails. You run it again; it passes. You run it a hundred times; it fails twice, on different lines. This section is about the tools and techniques that let you corner such bugs despite their unreliability.

### Typical Symptoms

Before we get to tools, let us catalogue the symptoms. Each symptom suggests a specific class of bug.

**Symptom: the test intermittently reports corrupted data, but the driver does not panic.** This is usually a missed lock: a field that is accessed without synchronization from more than one stream. The corruption is the result of the race; the driver survives because the corrupted value does not immediately cause a derivable failure.

**Symptom: the driver panics with a backtrace inside `mtx_lock` or `mtx_unlock`.** This is often a use-after-free of the mutex itself. The most common cause is that `mtx_destroy` was called while another thread was still using the mutex. The cure is to re-examine the detach path: the mutex must outlive every use of it.

**Symptom: `WITNESS` reports a lock-order reversal.** A thread acquired locks in an order inconsistent with an earlier-observed order. The fix is to define a global lock order and make every code path obey it.

**Symptom: `KASSERT` fires inside `mtx_assert(MA_OWNED)`.** A helper expected the mutex to be held and it was not. The fix is to find the call site of the helper and add the missing `mtx_lock`.

**Symptom: the test hangs forever.** A thread is sleeping on a channel and nobody ever calls `wakeup` on that channel. The fix is usually a missing `wakeup(&sc->cb)` in the I/O path (or equivalent). Chapter 10 Section 5 enumerated the rules.

**Symptom: the driver performs very poorly under load.** The mutex is contended more heavily than it needs to be. The fix is usually to shorten critical sections (move more work outside the lock) or to split the lock into finer pieces (Chapter 12 material).

**Symptom: the driver crashes weeks after deployment, with a backtrace that does not obviously involve concurrency.** This is the worst-case scenario and almost always a concurrency bug whose effects finally compounded. The fix is to audit as in Section 3 and run stress tests as in Section 6.

### INVARIANTS and KASSERT

`INVARIANTS` is a kernel build option that enables assertions throughout the kernel. When `INVARIANTS` is set, `KASSERT(cond, args)` evaluates `cond` and panics with `args` if the condition is false. When `INVARIANTS` is not set, `KASSERT` compiles to nothing.

This makes `KASSERT` nearly free in production and extremely valuable in development. Every invariant your code depends on should be a `KASSERT`. For example:

```c
KASSERT(sc->cb.cb_used <= sc->cb.cb_size,
    ("cbuf used exceeds size: %zu > %zu",
    sc->cb.cb_used, sc->cb.cb_size));
```

This says "I believe `cb_used` never exceeds `cb_size`. If the code that produces this truth is ever broken, I want to know immediately, not hours later when something else fails."

Add `KASSERT` calls liberally to your driver. Every precondition, every invariant, every "this cannot happen" branch. The cost is zero in production; the benefit is that development bugs get caught at the moment they appear, not downstream.

For the `myfirst` driver, some useful additions:

```c
/* In cbuf.c, at the top of cbuf_write: */
KASSERT(cb->cb_used <= cb->cb_size,
    ("cbuf_write: cb_used %zu exceeds cb_size %zu",
    cb->cb_used, cb->cb_size));
KASSERT(cb->cb_head < cb->cb_size,
    ("cbuf_write: cb_head %zu not less than cb_size %zu",
    cb->cb_head, cb->cb_size));

/* In myfirst_buf_read and myfirst_buf_write: */
mtx_assert(&sc->mtx, MA_OWNED);
```

Every such check is a small bet that catches a future mistake.

### WITNESS in Action

When `WITNESS` warns, the output is detailed. A typical warning for a lock-order reversal looks like this:

```text
lock order reversal:
 1st 0xfffffe000123a000 foo_lock (foo, sleep mutex) @ /usr/src/sys/dev/foo/foo.c:100
 2nd 0xfffffe000123a080 bar_lock (bar, sleep mutex) @ /usr/src/sys/dev/bar/bar.c:200
lock order reversal detected for lock group "bar" -> "foo"
stack backtrace:
...
```

The key pieces are:

- The two locks involved, with their names, types, and the source locations where they were acquired.
- The conflicting order (bar -> foo), which is the reverse of a previously-established order (foo -> bar).
- A stack backtrace showing the path that led to the reversal.

The fix is one of two things: either change one of the acquisition sites to match the existing order, or recognize that the two code paths should not both be holding both locks. The stack backtrace tells you which code path is involved; reading the source tells you which change is the right one.

For the `myfirst` driver, which has one lock, `WITNESS` cannot report a lock-order reversal. The only `WITNESS` warnings we could see relate to sleeping with a lock held or recursive acquisition. Both are worth testing deliberately, as we do in Lab 7.3.

### Reading a Kernel Panic Backtrace

Sometimes a bug produces a panic. The kernel prints a backtrace, enters the debugger (if `DDB` is configured), or reboots (if not). Your job is to extract as much information as possible from the backtrace before the system goes away.

The first lines of a panic typically identify the failure:

```text
panic: mtx_lock() of spin mutex @ ...: recursed on non-recursive mutex myfirst0 @ ...
cpuid = 2
...
```

From here:

- The panic message is the most important line. `mtx_lock of spin mutex` hints at one kind of bug; `sleeping with mutex held` at another; `general protection fault` at a different class entirely.
- The `cpuid` tells you which CPU experienced the panic, which can matter if the bug is specific to a particular scheduling environment.
- The stack backtrace shows the functions on the way down. Read it top-to-bottom: the bottom is where the panic was detected; the top is where the execution began. The function just above the panic function is usually the one that made the mistake.

If `DDB` is configured, you can interact with the debugger at panic time: `bt` (backtrace), `show mutex <addr>` (inspect a mutex), `show alllocks` (every lock held by every thread). This is the ninja mode of driver debugging; Chapter 12 and the debugging references touch it.

### dtrace(1) as a Silent Observer

`dtrace(1)` is FreeBSD's dynamic tracing framework. It lets you attach probes to kernel functions (and user-space functions, with the right libraries) and collect data with minimal impact on the observed code.

A simple `dtrace` command to count mutex acquisitions on `myfirst`'s mutex:

```sh
# dtrace -n 'fbt::__mtx_lock_flags:entry /arg0 != 0/ { @[execname] = count(); }'
```

Run the driver under load, then Ctrl-C the `dtrace`. You get a table of per-process lock acquisition counts. If one process dominates, that's the source of contention.

Another useful one-liner: trace when threads enter and exit our read handler:

```sh
# dtrace -n 'fbt::myfirst_read:entry { printf("%d reading %d bytes", tid, arg1); }'
```

The exact `arg` indices depend on the function's ABI; `dtrace -l | grep myfirst` lists the probes available.

`dtrace` is not magic: it uses real kernel mechanisms (typically the function-boundary-tracing provider) that have nonzero cost. But that cost is dramatically lower than `printf`-based logging, and it can be turned on and off without modifying the driver.

Chapter 15 will cover `dtrace` in more detail. For Chapter 11, the key idea is that `dtrace` is your friend for observing a live driver under load.

### A Debugging Checklist

When a concurrency bug appears, work through this checklist in order:

1. **Can you reproduce it reliably?** If yes, great; if no, run the test in a loop and find the failure rate.
2. **Does `WITNESS` report anything?** Boot with `options WITNESS INVARIANTS`. Rerun. Collect every warning.
3. **Are there `KASSERT` failures?** These fire as panics; the message identifies the invariant.
4. **Is the bug deterministic given sufficient stress?** If it appears every time with four writers and four readers, you have a workload to bisect; if it appears only under specific conditions, start isolating those conditions.
5. **Which field is corrupted?** Add targeted logging or `dtrace` probes around the suspect accesses.
6. **What is the missing synchronization?** Audit the access paths against the rules in Section 3.
7. **Is the fix consistent with the rest of the driver's locking strategy?** Document the change in `LOCKING.md`.
8. **Does the test now pass reliably?** Run the stress tests for twice as long as you would otherwise to confirm.

Most concurrency bugs yield to this process. Some require deep diving into the specific driver or the kernel; those are the ones Chapter 12 and later material help you prepare for.

### A Walkthrough: Diagnosing a Missing Wakeup

To make the debugging process concrete, let us walk through a hypothetical bug. Suppose you have modified `myfirst_write` to update some auxiliary state, and after the change, a two-terminal test (one `cat`, one `echo`) hangs. The `cat` is asleep, the `echo` has returned, and the bytes are not coming out.

Step 1: confirm the symptom.

```sh
$ ps -AxH -o pid,wchan,command | grep cat
12345  myfrd  cat /dev/myfirst
```

The `cat` is sleeping on `myfrd`. That is our sleep channel name. It is waiting for data.

Step 2: inspect the driver state.

```sh
$ sysctl dev.myfirst.0.stats.cb_used
dev.myfirst.0.stats.cb_used: 5
```

The buffer has 5 bytes in it. The reader should be able to drain those bytes and return them to `cat`. So why isn't it?

Step 3: look at the code. The `cat`'s thread is in `mtx_sleep(&sc->cb, ..., "myfrd", ...)`. It is waiting for `wakeup(&sc->cb)`. Who calls that?

Grep the source:

```sh
$ grep -n 'wakeup(&sc->cb' myfirst.c
180:        wakeup(&sc->cb);
220:        wakeup(&sc->cb);
```

Two call sites. One is in `myfirst_read` (after a successful read, to wake writers waiting for space). The other is in `myfirst_write` (after a successful write, to wake readers waiting for data).

Step 4: inspect the write path. Did the write actually run the `wakeup`?

```c
mtx_lock(&sc->mtx);
put = myfirst_buf_write(sc, bounce, want);
/* ... update aux state ... */
mtx_unlock(&sc->mtx);

/* new code: do some bookkeeping */
update_stats(sc, put);

wakeup(&sc->cb);
selwakeup(&sc->rsel);
```

Looks fine. The `wakeup` is there. The `selwakeup` is there.

Step 5: trace deeper. Maybe the `wakeup` is being called before the bytes are actually in the buffer. In the original code, the order was: add bytes under lock, unlock, wakeup. In the modified code, you inserted `update_stats` between the unlock and the `wakeup`. That should be fine; the bytes are in the buffer, just waiting for a waker to call `wakeup`.

But wait: `update_stats` is new. What does it do?

```c
static void
update_stats(struct myfirst_softc *sc, size_t n)
{
        mtx_lock(&sc->mtx);
        sc->aux_counter++;
        mtx_unlock(&sc->mtx);
}
```

It takes the mutex. And then releases it. That is a full acquire/release cycle. Could it change anything?

Actually, look closer. In the write path, the sequence is:

1. Take mutex.
2. Write bytes to cbuf.
3. Release mutex.
4. Call update_stats, which re-acquires and re-releases.
5. Call wakeup.

Between steps 3 and 5, there is a window where a reader could acquire the mutex, observe the new bytes, and proceed. In most cases this is fine: the reader does not care about the auxiliary statistics. But if the reader is currently inside `mtx_sleep`, waiting for `wakeup`, it is stuck until step 5.

That looks normal; step 5 always runs. So the `wakeup` does fire. But why is the reader not waking?

Step 6: add a `device_printf` at the start of `update_stats` and at the `wakeup` call:

```c
device_printf(sc->dev, "update_stats called\n");
/* ... */
device_printf(sc->dev, "wakeup(&sc->cb) called\n");
wakeup(&sc->cb);
```

Reload, retest. Watch `dmesg`:

```text
myfirst0: update_stats called
```

Only one message. The `wakeup` is never called.

Step 7: look at `update_stats` again. Could it have a path that returns early?

```c
static void
update_stats(struct myfirst_softc *sc, size_t n)
{
        if (n == 0)
                return;
        mtx_lock(&sc->mtx);
        sc->aux_counter++;
        mtx_unlock(&sc->mtx);
}
```

Ah, a short-circuit for `n == 0`. What if `put` was zero for some reason? Then `update_stats` returns. The caller continues, but wait, `wakeup` is still after `update_stats`, so it should fire anyway.

Unless the caller's code also has a short-circuit:

```c
update_stats(sc, put);
if (put < want)
        break;
wakeup(&sc->cb);
```

That's the bug. The early `break` skips the `wakeup`. Under most conditions `put == want`, and the `wakeup` runs. Under the rare condition where `put < want` (for example, the buffer filled up during this iteration), the `wakeup` is skipped. A reader waiting for data never sees the bytes that did make it in.

Step 8: fix it. Move the `wakeup` before the `break`:

```c
update_stats(sc, put);
wakeup(&sc->cb);        /* must happen even on short write */
selwakeup(&sc->rsel);
if (put < want)
        break;
```

Test again. The hang is gone.

This is a realistic debugging story. The bug is not in the concurrency machinery; it is in the business logic that controls when the wakeup fires. The fix is local. The debugging process was: observe, narrow, trace, inspect, fix.

### Patterns for Adding Debug Traces

When you add debug traces to a driver, several patterns pay off.

**Traces behind a debug flag.** We saw this in Chapter 10: a `myfirst_debug` integer controlled by sysctl, and a `MYFIRST_DBG` macro that compiles to nothing when the flag is zero. With the flag off, traces have no cost; with it on, they emit to `dmesg`. This lets you ship the driver with traces included and turn them on only when needed.

**One line per meaningful event.** The temptation is to trace every byte transferred. Resist it. Trace once per handler invocation, not once per byte. Trace once per lock acquisition during debugging of a specific bug, then remove. A log flooded with lines tells you nothing.

**Include the thread ID.** `curthread->td_tid` is the current thread's ID. Printing it in traces lets you distinguish concurrent activities in the log. Useful format: `device_printf(dev, "tid=%d got=%zu\n", curthread->td_tid, got)`.

**Include before/after state.** For a state change, log both the old and new values. `device_printf(dev, "cb_used: %zu -> %zu\n", before, after)` is more useful than `cb_used: %zu`, because you can see the transition.

**Remove or guard before shipping.** Trace lines are development aids. Either remove them or put them behind `MYFIRST_DBG` before the code is deployed. A shipping driver that emits debug output per I/O is wasting the log buffer.

### Debugging the Hard Cases

Some concurrency bugs resist all the techniques we have discussed. They reproduce on a specific machine, with a specific workload, on a specific kernel, and nowhere else. When you encounter such a bug, the options are:

**Bisect the workload.** If the bug occurs with `X` concurrent readers and `Y` concurrent writers, try to reduce `X` and `Y` until the bug no longer occurs. The minimal reproducer is the easiest to reason about.

**Bisect the kernel.** If the bug occurs on version N but not on N-1, find the change in the kernel that introduced it. `git bisect` is the tool. This is slow but effective.

**Inspect the hardware.** Some bugs are caused by specific CPU features (cache coherency quirks, TSO behavior, weakly-ordered memory models). If the bug occurs on ARM64 but not amd64, memory ordering is suspect.

**Ask on the mailing lists.** The FreeBSD community includes many people who have seen similar bugs. `freebsd-hackers` and `freebsd-current` lists welcome detailed bug reports. The more information you provide (exact kernel version, workload, failure mode, what you have tried), the more likely someone will recognize the pattern.

Concurrency debugging is, ultimately, a skill. The tools help, but the skill is built by doing it, writing tests that fail, writing fixes, and confirming the tests pass. The chapters ahead will give you more opportunities.

### Wrapping Up Section 7

Debugging concurrency is a trade of patience for tooling. `INVARIANTS`, `WITNESS`, `KASSERT`, `mtx_assert`, and `dtrace` are the tools FreeBSD gives you. Used in combination, they can corner bugs that would otherwise go undetected until production.

Section 8 closes the chapter with the housekeeping: refactoring the driver, versioning it, documenting it, running static analysis, and regression-testing the whole lot.



## Section 8: Refactoring and Versioning Your Concurrent Driver

The driver now has well-understood concurrency semantics and a locking strategy that can be defended from first principles. This final section of the chapter is the hygiene pass: organizing the code for clarity, versioning the driver so future-you can tell what changed when, writing a README that documents the design, running static analysis, and regression-testing the whole thing.

This is not glamorous work. It is also what separates a driver that is delivered once from a driver that lives usefully for years.

### 8.1: Organizing the Code for Clarity

The Stage 4 driver from Chapter 10 was already well-organized. The cbuf is in its own file. The I/O handlers use helper functions. The locking discipline is documented in a comment at the top. For Chapter 11, the organizational work is marginal: improving commenting, grouping related code, and ensuring naming consistency.

Three improvements are worth making.

**Group related code.** Reorder the functions in `myfirst.c` so related ones live next to each other. Lifecycle (attach, detach, modevent) at the top; I/O handlers (read, write, poll) in the middle; helpers and sysctl callbacks near the end. The compile order does not matter; what matters is that a reader scrolling through the file encounters related functions together.

**Isolate locking into inline wrappers.** Instead of repeating `mtx_lock(&sc->mtx)` and `mtx_unlock(&sc->mtx)` throughout the code, define:

```c
#define MYFIRST_LOCK(sc)        mtx_lock(&(sc)->mtx)
#define MYFIRST_UNLOCK(sc)      mtx_unlock(&(sc)->mtx)
#define MYFIRST_ASSERT(sc)      mtx_assert(&(sc)->mtx, MA_OWNED)
```

Use the macros throughout. If you ever need to change the lock type (for example, to an `sx` lock in a future refactor), you change one place, not twenty.

**Name things consistently.** All driver-level buffer helpers are `myfirst_buf_*`. All wait helpers are `myfirst_wait_*`. All sysctl handlers are `myfirst_sysctl_*`. A reader scanning the function names can tell what category each function falls into without reading the body.

None of these is a correctness improvement. All of them are readability improvements that pay off when you come back to the code in six months.

### 8.2: Versioning the Driver

The driver should expose its version so that you can tell at load time what you are running. Add a version string:

```c
#define MYFIRST_VERSION "0.4-concurrency"
```

Print it at attach:

```c
device_printf(dev,
    "Attached; version %s, node /dev/%s (alias /dev/myfirst), "
    "cbuf=%zu bytes\n",
    MYFIRST_VERSION, devtoname(sc->cdev), cbuf_size(&sc->cb));
```

Expose it as a read-only sysctl:

```c
SYSCTL_STRING(_hw_myfirst, OID_AUTO, version, CTLFLAG_RD,
    MYFIRST_VERSION, 0, "Driver version");
```

A user can now query `sysctl hw.myfirst.version` or check `dmesg` to confirm which version is loaded. When debugging, this removes any doubt about which code is running.

Pick a versioning scheme and stick with it. Semantic versioning (major.minor.patch) is fine. Date-based (2026.04) is fine. Book-chapter-based (0.1 after Chapter 7, 0.2 after Chapter 8, 0.3 after Chapter 9, 0.4 after Chapter 10, 0.5 after Chapter 11) is fine and is what the companion source uses. The important property is consistency, not specifics.

For Chapter 11 specifically, the appropriate version bump is to `0.5-concurrency`. The changes are: counter(9) migration, KASSERTs added, LOCKING.md added, annotations added. All are safety and clarity changes; none is a behavioural change visible from user space. Document them in a `CHANGELOG.md`:

```markdown
# myfirst Changelog

## 0.5-concurrency (Chapter 11)
- Migrated bytes_read, bytes_written to counter_u64_t (lock-free).
- Added KASSERTs throughout cbuf_* helpers.
- Added LOCKING.md documenting the locking strategy.
- Added source annotations naming each critical section.
- Added MYFIRST_LOCK/UNLOCK/ASSERT macros for future lock changes.

## 0.4-poll-refactor (Chapter 10, Stage 4)
- Added d_poll and selinfo.
- Refactored I/O handlers to use wait helpers.
- Added locking-strategy comment.

## 0.3-blocking (Chapter 10, Stage 3)
- Added mtx_sleep-based blocking read/write paths.
- Added IO_NDELAY -> EAGAIN handling.

## 0.2-circular (Chapter 10, Stage 2)
- Replaced linear FIFO with cbuf circular buffer.

## 0.1 (Chapter 9)
- Initial read/write via uiomove.
```

A `CHANGELOG.md` you can consult beats git history when you want the quick answer. Keep it updated with each change.

### 8.3: README and Comment Pass

Alongside `LOCKING.md`, write a `README.md` for the driver. The audience is a future maintainer (possibly you) who has just checked out the source and needs to know what the project is.

A minimal version:

```markdown
# myfirst

A FreeBSD 14.3 pseudo-device driver that demonstrates buffered I/O,
concurrency, and modern driver conventions. Developed as the running
example for the book "FreeBSD Device Drivers: From First Steps to
Kernel Mastery."

## Status

Version 0.5-concurrency (Chapter 11).

## Features

- A Newbus pseudo-device under nexus0.
- A primary device node at /dev/myfirst/0 (alias: /dev/myfirst).
- A circular buffer (cbuf) as the I/O buffer.
- Blocking and non-blocking reads and writes.
- poll(2) support via d_poll and selinfo.
- Per-CPU byte counters via counter(9).
- A single sleep mutex protects composite state; see LOCKING.md.

## Build and Load

    $ make
    # kldload ./myfirst.ko
    # dmesg | tail
    # ls -l /dev/myfirst
    # printf 'hello' > /dev/myfirst
    # cat /dev/myfirst
    # kldunload myfirst

## Tests

See ../../userland/ for the test programs. The most useful one is
producer_consumer, which exercises the round-trip correctness of
the circular buffer.

## License

BSD 2-Clause. See individual source files for SPDX headers.
```

Every driver benefits from a README like this. Without it, a new maintainer (possibly you, six months from now) has to reverse-engineer what the project is. With it, the onboarding is minutes.

In parallel, do a comment pass on the source itself. Focus on the critical sections: every `mtx_lock` should have a brief comment naming the shared state it is about to protect. Every helper should have a one-line description above the function definition. Every non-obvious piece of arithmetic (cbuf wrap-around, `nbefore` comparison, etc.) should have a sentence of explanation.

The goal is not exhaustive commentary. It is to make the code self-explanatory to a reader who has not seen it before.

### 8.4: Static Analysis

FreeBSD's base system includes `clang`, which has a `--analyze` mode that performs static analysis without compiling. For a kernel module, invoke it via:

```sh
$ make WARNS=6 CFLAGS+="-Weverything -Wno-unknown-warning-option" clean all
```

Or, more directly:

```sh
$ clang --analyze -I/usr/src/sys -I/usr/src/sys/amd64/conf/GENERIC \
    -D_KERNEL myfirst.c
```

The output is a list of potential issues with file-line annotations. Triage them: false positives (clang does not understand some kernel idioms) are fine to ignore; genuine issues (uninitialized variables, null-pointer dereferences, memory leaks) deserve fixes.

Add a `lint` target to your `Makefile`:

```makefile
.PHONY: lint
lint:
	cd ${.CURDIR}; clang --analyze -D_KERNEL *.c
```

Run `make lint` periodically. A clean run is the baseline; any new warning deserves attention before being merged.

### 8.5: Regression Testing

Assemble a regression test that runs every verification you have built. A shell script in the `tests/` subdirectory:

```sh
#!/bin/sh
# regression.sh: run every test from Chapters 7-11 in sequence.

set -eu

die() { echo "FAIL: $*" >&2; exit 1; }
ok()  { echo "PASS: $*"; }

# Preconditions
[ $(id -u) -eq 0 ] || die "must run as root"
kldstat | grep -q myfirst && kldunload myfirst
[ -f ./myfirst.ko ] || die "myfirst.ko not built; run make first"

kldload ./myfirst.ko
trap 'kldunload myfirst 2>/dev/null || true' EXIT

sleep 1
[ -c /dev/myfirst ] || die "device node not created"
ok "load"

printf 'hello' > /dev/myfirst || die "write failed"
cat /dev/myfirst >/tmp/out.$$
[ "$(cat /tmp/out.$$)" = "hello" ] || die "round-trip content mismatch"
rm -f /tmp/out.$$
ok "round-trip"

cd ../userland && make -s clean && make -s && cd -

../userland/producer_consumer || die "producer_consumer failed"
ok "producer_consumer"

../userland/mp_stress || die "mp_stress failed"
ok "mp_stress"

sysctl dev.myfirst.0.stats >/dev/null || die "sysctl not accessible"
ok "sysctl"

echo "ALL TESTS PASSED"
```

Run this after every change:

```sh
# ./tests/regression.sh
PASS: load
PASS: round-trip
PASS: producer_consumer
PASS: mp_stress
PASS: sysctl
ALL TESTS PASSED
```

A green regression is the evidence that the change did not break anything. Pair it with the debug-kernel stress runs and you have a reasonable quality gate for a driver at this stage of the book.

### A Complete LOCKING.md Template

For readers who want a more substantive starting template than the minimal one in Section 3, here is a fuller `LOCKING.md` that can be adapted to any driver:

```markdown
# <driver-name> Locking Strategy

## Overview

One sentence describing the overall approach. For example:
"The driver uses a single sleep mutex to serialize all access to
shared state, with separate per-CPU counters for hot-path
statistics."

## Locks Owned by This Driver

### sc->mtx (mutex(9), MTX_DEF)

**Protects:**
- sc->cb (circular buffer state: cb_head, cb_used, cb_data)
- sc->active_fhs
- sc->is_attached (writes)
- sc->rsel, sc->wsel (indirectly: selrecord inside a critical
  section; selwakeup outside)

**Wait channels used under this mutex:**
- &sc->cb: data available / space available

## Locks Owned by Other Subsystems

- selinfo internal locks: handled by the selinfo API; we must
  call selrecord under our lock and selwakeup outside.

## Unprotected Accesses

### sc->is_attached (reads at handler entry)

**Why it's safe:**
A stale "true" is re-checked after every sleep; a stale "false"
merely causes the handler to return ENXIO early, which is
harmless since the device really is gone.

### sc->bytes_read, sc->bytes_written (counter(9) fetches)

**Why it's safe:**
counter(9) handles its own consistency internally. Fetches may
be slightly stale but are never torn.

## Lock Order

The driver currently holds only one lock. No ordering rules apply.

When new locks are added, document the order here before merging
the change. The format is:

  sc->mtx -> sc->other_mtx

meaning: a thread holding sc->mtx may acquire sc->other_mtx,
but not the reverse.

## Rules

1. Never sleep while holding sc->mtx except via mtx_sleep, which
   atomically drops and reacquires.
2. Never call uiomove, copyin, copyout, selwakeup, or wakeup
   while holding sc->mtx.
3. Every cbuf_* call must happen with sc->mtx held (the helpers
   assert this with mtx_assert).
4. The detach path clears sc->is_attached under the mutex and
   then wakes any sleepers before destroying the mutex.

## History

- 0.5-concurrency (Chapter 11): migrated byte counters to
  counter(9); added mtx_assert calls; formalized the strategy.
- 0.4-poll-refactor (Chapter 10): added d_poll, refactored into
  helpers, documented the initial strategy inline in the source.
- Earlier versions: strategy was implicit in the code.
```

A document this shape is auditable. Any future change to the driver's concurrency story touches this file in the same commit. The review diff shows the rule change, not just the code change.

### A Pre-Commit Checklist

Before committing a concurrency-affecting change, run through this checklist:

1. **Have I updated `LOCKING.md`?** If the change adds a field to shared state, names a new lock, changes lock ordering, adds a wait channel, or changes a rule, update the doc.
2. **Have I run the full regression suite?** On a debug kernel, with `WITNESS` and `INVARIANTS`. A green regression is the minimum.
3. **Have I run the stress tests?** Not just once; for long enough to matter. Thirty minutes is a reasonable minimum for a significant change.
4. **Have I run `clang --analyze`?** Treat new warnings as bugs.
5. **Have I added a `KASSERT` for any new invariant?** Every invariant your code assumes should be a `KASSERT`.
6. **Have I bumped the version string and updated `CHANGELOG.md`?** Even if the change is small. Future you will thank present you.
7. **Have I verified the test kit compiles?** The test programs are part of the change; they should not bitrot.

Most changes clear the checklist in minutes. The changes that do not are the ones that should be caught by the checklist: those are the ones most likely to regress.

### Versioning Strategy: Why 0.x?

We are using a 0.x scheme for the driver because the driver is not yet feature-complete. A 1.0 release implies stability and completeness that we cannot claim. The 0.x versions track our progress through the book:

- 0.1: basic scaffold (Chapter 7).
- 0.2: device file and per-open state (Chapter 8).
- 0.3: basic read/write via uiomove (Chapter 9).
- 0.4: buffered I/O, non-blocking, poll (Chapter 10).
- 0.5: concurrency hardening (Chapter 11).

Chapter 12 will introduce 0.6. Eventually, when the driver is feature-complete and stable, we would call it 1.0.

This is not the only sensible scheme. Semantic versioning (`MAJOR.MINOR.PATCH`) is appropriate for drivers with a stable API. Date-based versioning (`2026.04`) is appropriate for drivers where the release cadence is temporal. The choice matters less than the commitment: pick one, apply it consistently, document it in the README.

### A Note on Git Hygiene

The companion source tree is organized so that each chapter's stage lives in its own directory. You can build any stage independently; you can diff stage-over-stage to see what changed. If you are using git, a clean commit history further enhances this:

- One commit per logical change.
- Commit messages that explain *why*, not just *what*.
- A separate commit for `LOCKING.md` updates when the code changes.

A git log like:

```text
0.5 add KASSERT for cbuf_write overflow invariant
0.5 migrate bytes_read, bytes_written to counter(9)
0.5 document locking strategy in LOCKING.md
0.5 add MYFIRST_LOCK/UNLOCK/ASSERT macros
0.4 refactor I/O handlers to use wait helpers
```

is readable years later. A log like:

```text
fix stuff
more fixes
wip
```

is not. Your driver is a living artifact; treat its history as documentation.

### Wrapping Up Section 8

The driver is now not only correct but *verifiably* correct and *maintainably* organized. It has:

- A documented locking strategy in `LOCKING.md`.
- A versioning scheme and a changelog.
- A README for future maintainers.
- Source annotations for every critical section.
- Static analysis via `clang --analyze`.
- A regression test that runs everything.

That is a substantial body of infrastructure. Most beginner drivers have none of it. Yours now does, and the work to maintain it is routine rather than heroic.



## Hands-on Labs

These labs consolidate the concepts of Chapter 11 through direct, hands-on experience. They are ordered from least to most demanding. Each is designed to be completable in a single lab session.

### Pre-Lab Setup Checklist

Before starting any lab, confirm the four items below. Two minutes of preparation here will save you significant time when something goes wrong inside a lab and you cannot tell whether the problem is your code, your environment, or your kernel.

1. **Debug kernel running.** Confirm with `sysctl kern.ident` that the booted kernel is the one with `INVARIANTS` and `WITNESS` enabled. The reference section "Building and Booting a Debug Kernel" later in this chapter walks through the build, install, and reboot if you have not already done it.
2. **WITNESS active.** Run `sysctl debug.witness.watch` and confirm a non-zero value. If the sysctl is missing, your kernel does not have `WITNESS` compiled in; rebuild before continuing.
3. **Driver source matches Chapter 10 Stage 4.** From your driver directory, `make clean && make` should compile cleanly. The `myfirst.ko` artefact must exist before any lab attempts to load it.
4. **A clean dmesg.** Run `dmesg -c >/dev/null` once before the first lab so any prior warnings do not confuse you. Subsequent `dmesg` checks then show only what your lab produced.

If any of the four is uncertain, fix it now. The labs assume all four.

### Lab 11.1: Observe a Race Condition

**Goal.** Build the race-demo driver from Section 2, run it, and observe the corruption. The purpose is not to produce a correct driver; it is to see with your own eyes what "no synchronization" means.

**Steps.**

1. Create `examples/part-03/ch11-concurrency/stage1-race-demo/`.
2. Copy `cbuf.c`, `cbuf.h`, and the `Makefile` from the Chapter 10 Stage 4 directory.
3. Copy `myfirst.c` and add the `RACE_DEMO` macros at the top as shown in Section 2. Replace every `mtx_lock`, `mtx_unlock`, and `mtx_assert` in the I/O paths with the `MYFIRST_LOCK`, `MYFIRST_UNLOCK`, `MYFIRST_ASSERT` macros. Compile with `-DRACE_DEMO` (which is the default in this directory's Makefile).
4. Build and load.
5. In one terminal, run `../../part-02/ch10-handling-io-efficiently/userland/producer_consumer`.
6. Observe the output. Record the mismatch count.
7. Run it several times. Observe that the mismatch count varies.
8. Unload the driver.

**Verification.** Every run produces at least one mismatch. The exact number varies. This demonstrates that concurrency bugs are not a theoretical possibility; they are the certainty when locks are absent.

**Stretch goal.** Add a `device_printf` inside `cbuf_write` that logs when `cb_used` exceeds `cb_size`. Load the driver, run the test, and observe the log messages. Each message is an invariant violation caught in flight.

### Lab 11.2: Verify the Locking Discipline with INVARIANTS

**Goal.** Convert your Chapter 10 Stage 4 driver to use the `MYFIRST_LOCK` macros and add `mtx_assert` calls throughout. Run it under a `WITNESS`-enabled kernel and confirm that no warnings appear.

**Steps.**

1. Copy your Stage 4 driver into `examples/part-03/ch11-concurrency/stage2-concurrent-safe/`.
2. Add the `MYFIRST_LOCK`, `MYFIRST_UNLOCK`, `MYFIRST_ASSERT` macros at the top. Make them expand to the real `mtx_*` calls (no `RACE_DEMO`).
3. Replace the `mtx_lock`, `mtx_unlock`, `mtx_assert` calls in the I/O paths with the macros.
4. Add `MYFIRST_ASSERT(sc)` at the top of every helper that should be called with the lock held.
5. Build with `WARNS=6`.
6. Load on a `WITNESS`-enabled kernel.
7. Run the Chapter 10 test kit (`producer_consumer`, `rw_myfirst_nb`, `mp_stress`).
8. Confirm no `WITNESS` warnings appear in `dmesg`.

**Verification.** `dmesg | grep -i witness` shows no output related to `myfirst`. Every test passes.

**Stretch goal.** Introduce a deliberate bug: remove one `mtx_unlock` call, leaving the lock held past the end of the handler. Observe the `WITNESS` warning that appears, confirm it identifies the exact lock and the approximate line, then restore the unlock.

### Lab 11.3: Migrate to counter(9)

**Goal.** Replace the `sc->bytes_read` and `sc->bytes_written` fields with `counter(9)` per-CPU counters.

**Steps.**

1. Copy Lab 11.2's source into `stage3-counter9/`.
2. Change the struct definition so the two fields are `counter_u64_t`.
3. Update `myfirst_attach` to allocate them and `myfirst_detach` to free them.
4. Update `myfirst_buf_read` and `myfirst_buf_write` to use `counter_u64_add`.
5. Remove the field updates from under the mutex (they no longer need the mutex).
6. Update the `sysctl` handlers to use `counter_u64_fetch`.
7. Update `LOCKING.md` to note that `bytes_read` and `bytes_written` are no longer under the mutex.
8. Build, load, and run the test kit.

**Verification.** `sysctl dev.myfirst.0.stats.bytes_read` and `bytes_written` still return correct values. No test fails.

**Stretch goal.** Run `mp_stress` with 8 writers and 8 readers for 60 seconds on a multi-core machine. Compare throughput with Lab 11.2 (mutex-protected counters). The per-CPU counters should be measurably faster if the machine has at least 4 cores.

### Lab 11.4: Build a Multi-threaded Tester

**Goal.** Build `mt_reader` from Section 6. Use it to exercise the driver with multiple threads in one process.

**Steps.**

1. Under `examples/part-03/ch11-concurrency/userland/`, type `mt_reader.c` from Section 6, or open the companion source file already present in that directory and read it carefully.
2. From the same directory, run `make mt_reader` (the Makefile already includes the right `-pthread` linkage). The standalone command is `cc -Wall -Wextra -pthread -o mt_reader mt_reader.c`.
3. Start a writer in another terminal: `dd if=/dev/zero of=/dev/myfirst bs=4k &`.
4. Run `./mt_reader`.
5. Confirm that each thread sees a nonzero byte count and that the sum is consistent with what the writer produced.
6. When you are done, stop the background `dd` with `kill %1` (or whatever job number it has).

**Verification.** All four threads report a nonzero count. The grand total equals the writer's byte count minus the buffer fill at the end.

**Stretch goal.** Increase `NTHREADS` to 8, 16, 32. Observe the scaling behavior. Does throughput increase, decrease, or remain flat? Explain why (hint: Section 5's discussion of lock contention).

### Lab 11.5: Deliberately Cause a WITNESS Warning

**Goal.** Introduce a deliberate concurrency bug and observe how `WITNESS` catches it.

**Steps.**

1. Copy your Stage 3 working directory into a scratch directory such as `stage-lab11-5/`. Do *not* edit Stage 3 in place; the purpose of the lab is to revert cleanly.
2. In `myfirst_read`, move the `MYFIRST_UNLOCK(sc)` call from before `uiomove` to after `uiomove`. The code still compiles, but the mutex is now held across the `uiomove`, which may sleep.
3. Build and load on a `WITNESS`-enabled debug kernel (see the "Building and Booting a Debug Kernel" reference later in this chapter).
4. Run `producer_consumer` in one terminal and watch `dmesg` in another.
5. Observe the warning emitted by `WITNESS` when the `uiomove` is about to sleep while `sc->mtx` is held. Record the exact message and the source line it cites.
6. Unload the module, delete the scratch directory, and confirm that your Stage 3 source is still correct.

**Verification.** `dmesg` shows a warning similar to "acquiring duplicate lock of same type" or "sleeping thread (pid N) owns a non-sleepable lock" pointing at the line where the unlock used to live.

**Stretch goal.** Repeat with a different deliberate bug: change the mutex type from `MTX_DEF` to `MTX_SPIN` in `mtx_init`. Observe the resulting warnings, which should be about sleeping while holding a spin mutex. Again, revert cleanly when you are done.

### Lab 11.6: Add KASSERTs Throughout

**Goal.** Add `KASSERT` calls to the cbuf helpers and the driver code. Confirm they fire when deliberately triggered.

**Steps.**

1. Copy Lab 11.3's source into `stage5-kasserts/`.
2. In `cbuf_write`, add `KASSERT(cb->cb_used + n <= cb->cb_size, ...)` at the top.
3. In `cbuf_read`, add `KASSERT(n <= cb->cb_used, ...)`.
4. In `myfirst_buf_*` helpers, add `mtx_assert(&sc->mtx, MA_OWNED)`.
5. Build and load on an `INVARIANTS` kernel.
6. Run the test kit. No assertions should fire in normal operation.
7. Introduce a bug: in `cbuf_write`, write `cb->cb_used += n + 1` instead of `cb->cb_used += n`. Confirm that the `KASSERT` in the next call catches the overflow.
8. Revert the bug.

**Verification.** Every assertion in the reverted source compiles and the driver works normally. The deliberate bug produces a panic with a clear backtrace that names the failed `KASSERT`.

### Lab 11.7: Long-Running Stress

**Goal.** Run the full Chapter 11 test kit for one hour and verify no failures, no kernel warnings, and no memory growth.

**Steps.**

1. Load your Chapter 11 Stage 3 or Stage 5 driver.
2. In one terminal, run:
   ```sh
   for i in $(seq 1 60); do
     ./producer_consumer
     ./mp_stress
   done
   ```
3. In a second terminal, run a background `vmstat 10` and `sysctl dev.myfirst.0.stats` periodically.
4. Monitor `dmesg` for warnings.
5. After the loop finishes, check `vmstat -m | grep cbuf` and confirm memory has not grown.

**Verification.** All loops complete. No `WITNESS` warnings. `vmstat -m | grep cbuf` shows the expected constant allocation. The byte counters in `sysctl` have increased; their ratio of `bytes_read` to `bytes_written` is approximately 1.

### Lab 11.8: Run clang --analyze

**Goal.** Run static analysis on the driver and triage the results.

**Steps.**

1. From your Chapter 11 Stage 3 or Stage 5 directory, run:
   ```sh
   clang --analyze -D_KERNEL -I/usr/src/sys \
       -I/usr/src/sys/amd64/conf/GENERIC myfirst.c
   ```
2. Record every warning.
3. For each warning, classify it as (a) a true positive (real bug), (b) a false positive that you understand, or (c) a warning you do not understand.
4. For (a), fix the bug. For (b), document why it is a false positive. For (c), research until it moves to (a) or (b).

**Verification.** After the pass, the driver has zero unclassified warnings. The `LOCKING.md` or `README.md` documents any known false positives.



## Challenge Exercises

The challenges extend Chapter 11's concepts beyond the baseline labs. Each is optional; each is designed to deepen your understanding.

### Challenge 1: A Lock-Free Producer/Consumer

The mutex we use protects composite state. A single-producer-single-consumer queue can be implemented lock-free, using only atomics, because the writer touches only the tail and the reader touches only the head, and neither touches the fields the other updates. `buf_ring(9)` builds on this idea but goes further: it uses compare-and-swap so several producers can enqueue concurrently, and offers either single-consumer or multi-consumer dequeue paths chosen at allocation time.

Read `/usr/src/sys/sys/buf_ring.h`, where the lock-free enqueue and dequeue paths live as inline functions, and `/usr/src/sys/kern/subr_bufring.c`, which holds `buf_ring_alloc()` and `buf_ring_free()`. Understand the `buf_ring(9)` primitive. Then, as an exercise, build a variant of the driver where the circular buffer is replaced by a `buf_ring`. The read and write handlers no longer need a mutex for the ring itself; they may still need one for other state.

This challenge is harder than it looks. Attempt it only if you are comfortable with Chapter 11's material and want a stretch.

### Challenge 2: Multi-Mutex Design

Split `sc->mtx` into two locks: `sc->state_mtx` (protects `is_attached`, `active_fhs`, `open_count`) and `sc->buf_mtx` (protects the cbuf and byte counters). Define a lock order (for example, state before buf) and document it in `LOCKING.md`. Update all the handlers.

What do you gain? What do you lose? Is there a workload that measurably benefits?

### Challenge 3: WITNESS Under Pressure

Construct a test that provokes a `WITNESS` warning even on a correctly-locked driver. (Hint: `sx` locks have different rules; mixing a mutex and an `sx` lock in conflicting orders produces warnings. Chapter 12 covers `sx`.) This challenge is a preview of Chapter 12 material.

### Challenge 4: Instrument with dtrace

Write a `dtrace` script that:

- Counts the number of `mtx_lock(&sc->mtx)` calls per second.
- Prints a histogram of the time spent holding the lock.
- Correlates lock-holding time with `bytes_read` and `bytes_written` rates.

Run the script while `mp_stress` is running. Produce a report.

### Challenge 5: Bounded Blocking

Modify `myfirst_read` to return `EAGAIN` if the read would block for more than N milliseconds, where N is a sysctl-tunable parameter. (Hint: the `timo` argument of `mtx_sleep` is in ticks, where `hz` ticks equal one second. For example, `hz / 10` is roughly 100 milliseconds. For sub-tick precision, the `msleep_sbt(9)` family uses `sbintime_t` and the `SBT_1S`, `SBT_1MS`, and `SBT_1US` constants from `sys/time.h`.) This challenge previews some of Chapter 12's material on timeouts.

### Challenge 6: Write a Lock-Contention Benchmark

Build a benchmark that measures the mutex acquisition rate under varying load:

- 1 writer, 1 reader.
- 1 writer, 4 readers.
- 4 writers, 4 readers.
- 8 writers, 8 readers.

Report acquisition-per-second, average hold time, and maximum wait time. Use `dtrace` or the mutex profiling sysctl.

### Challenge 7: Port to a 32-bit Platform

FreeBSD still supports 32-bit architectures (i386, armv7). Build your driver for one of them and run the test kit. Do any of the torn-read issues from Section 3 manifest? Is the per-CPU counter migration from Lab 11.3 necessary for correctness, or merely for performance?

(This challenge is genuinely hard and requires access to 32-bit hardware or a cross-build environment.)

### Challenge 8: Add a Per-Descriptor Mutex

The per-descriptor `struct myfirst_fh` has `reads` and `writes` counters that are currently updated under the device-wide `sc->mtx`. An alternative is to give each `fh` its own mutex. Would this help? Hurt? Write the change, run the benchmark, report.



## Troubleshooting

This reference catalogues the bugs you are most likely to encounter while working through Chapter 11 and after. Each entry has a symptom, a cause, and a fix.

### Symptom: `mtx_assert` panic on a driver that used to work

**Cause.** A helper was called without the mutex held. This usually happens when you refactor and move a call outside the critical section.

**Fix.** Look at the backtrace; the top frame names the helper. Find the caller. Ensure the caller holds `sc->mtx` at the point of the call.

### Symptom: `sleepable after non-sleepable acquired` warning

**Cause.** A call that might sleep (typically `uiomove`, `copyin`, `copyout`, `malloc(... M_WAITOK)`) was made while holding a non-sleepable lock. On FreeBSD, `MTX_DEF` is sleepable; the warning is usually more specific.

**Fix.** Drop the mutex before the sleeping call. If state must be preserved, take a snapshot under the mutex, do the sleeping call outside, and reacquire to commit.

### Symptom: Lock-order reversal

**Cause.** Two acquisition orders observed; one is the reverse of the other. Only possible with two or more locks.

**Fix.** Define a global order. Update the offending path. Document in `LOCKING.md`.

### Symptom: Deadlock (hang)

**Cause.** Either a missing `wakeup(&sc->cb)` or a sleep with a different channel than the wake.

**Fix.** Check every `mtx_sleep` channel against every `wakeup` channel. They must match exactly. `&sc->cb` sleeping and `&sc->buf` waking are different channels.

### Symptom: Torn value read from a `uint64_t`

**Cause.** On a 32-bit platform, a 64-bit read is two 32-bit reads, which can straddle a concurrent write. On amd64, aligned 64-bit accesses are atomic; on 32-bit platforms, they are not.

**Fix.** Either protect with the mutex (simple) or migrate to `counter(9)` (scalable) or use the `atomic_*_64` primitives (precise).

### Symptom: Double-free panic on the cbuf

**Cause.** `cbuf_destroy` called twice, typically because the error path in `myfirst_attach` runs `cbuf_destroy` and then `myfirst_detach` runs it again.

**Fix.** After calling `cbuf_destroy`, set `sc->cb.cb_data = NULL` so a second call is a no-op. Or guard the destroy with an explicit flag.

### Symptom: Slow performance under many-core load

**Cause.** Mutex contention. Every CPU is serializing on `sc->mtx`.

**Fix.** Migrate byte counters to `counter(9)` (Lab 11.3). If further reduction is needed, split the mutex (Challenge 2) or use finer-grained primitives (Chapter 12).

### Symptom: `producer_consumer` occasionally hangs

**Cause.** A race between the writer finishing and the reader deciding the buffer is "permanently empty". Our driver returns zero bytes on an empty non-blocking read, which `producer_consumer` may interpret as EOF.

**Fix.** Use blocking reads in `producer_consumer` (remove `O_NONBLOCK` if it was added). Ensure the writer closes the descriptor or the reader detects end-of-test by total byte count, not by a zero-byte read.

### Symptom: `WITNESS` reports "unowned lock released"

**Cause.** `mtx_unlock` called without a matching `mtx_lock` earlier in the same thread.

**Fix.** Audit every `mtx_unlock` site. Trace back and confirm each has a matching `mtx_lock` before it, along every path.

### Symptom: Compiles cleanly, passes single-threaded tests, fails `mp_stress`

**Cause.** A missed lock somewhere on the data path. Single-threaded tests never exercise the race; concurrent tests do.

**Fix.** Audit every access to shared state, as in Section 3. The usual culprit is a field updated outside the critical section because "it's only a counter" (not true under concurrency).

### Symptom: `kldunload` hangs with descriptors open

**Cause.** The detach path refuses to proceed while `active_fhs > 0`.

**Fix.** `fstat | grep myfirst` to find the offending processes. `kill` them. Then retry `kldunload`.

### Symptom: Kernel panic with a clean backtrace in `mtx_lock`

**Cause.** The mutex was destroyed and its memory reused. The atomic operation in `mtx_lock` is operating on whatever happens to be in that memory now.

**Fix.** Ensure `mtx_destroy` is called only after every handler that could use the mutex has returned. Our detach path does this by destroying the cdev first, which waits for in-flight handlers.

### Symptom: `WITNESS` never warns, even when you expect it to

**Cause.** Three possibilities. First, the running kernel does not actually have `WITNESS` compiled in; check `sysctl debug.witness.watch` and confirm a non-zero value. Second, `WITNESS` is enabled but quiet because no rule has been violated yet; not every refactor produces a violation. Third, the offending code path was not exercised; `WITNESS` only checks the orderings the kernel actually observes at runtime.

**Fix.** Confirm the kernel has `WITNESS` (the `sysctl` above is the easy check). Then drive the suspect code path with the multi-process and multi-thread testers from Section 6 so the kernel sees the acquisitions in question.

### Symptom: `dmesg` is silent but the test still fails

**Cause.** Either the failure is in user-space (the test program) or the kernel did detect a problem but printed nothing because the message buffer wrapped. The latter is rare on a freshly-booted system but common on long-running test machines.

**Fix.** Increase the buffer for next boot by adding `kern.msgbufsize="4194304"` to `/boot/loader.conf`; the sysctl is read-only at runtime. For user-space failures, run the test under `truss(1)` to see the syscalls and their return values; concurrency bugs at the user-space level often surface as `EAGAIN`, `EINTR`, or short reads that the test mishandles.



## Wrapping Up

Chapter 11 took the driver you built in Chapter 10 and placed it on a foundation of concurrency understanding. We did not change its behaviour; we built the understanding that lets you change its behaviour safely in the future.

We started with the premise that concurrency is the default environment every driver lives in, and that a driver which ignores that fact is a driver waiting to fail on a user's faster machine, busier workload, or larger core count. The four failure modes (data corruption, lost updates, torn values, inconsistent composite state) are not rare edge cases; they are the certainty when multiple streams touch shared state without coordination.

We then built the tools that prevent those failures. Atomics, for the small and simple cases. Mutexes, for everything else. We applied the tools to the driver: per-CPU counters replaced the mutex-protected byte counters, the single mutex covers the cbuf and its composite invariants, and `mtx_sleep` provides the blocking path that Chapter 10 used without explanation.

We learned to verify. `INVARIANTS` and `WITNESS` catch sleeping-with-mutex and lock-order violations at development time. `KASSERT` and `mtx_assert` document invariants and catch their violations. `dtrace` observes without disturbing. A multi-threaded and multi-process test suite exercises the driver at levels the base-system tools cannot reach.

We refactored for maintainability. The locking strategy is documented in `LOCKING.md`. The versioning is explicit. The README introduces the project to a new reader. The regression test runs every test we have built.

The driver is now the most robust character-device driver many beginners have ever written. It is also small, comprehensible, and source-referenced. Every locking decision has a first-principles justification. Every test has a specific purpose. Every piece of infrastructure supports the next chapter's work.

Three closing reminders before moving on.

The first is to *run the tests*. Concurrency bugs do not make themselves obvious. If you have not run `mp_stress` for an hour on a debug kernel, you have not confirmed that your driver is correct; you have only confirmed that your tests cannot find a bug in the time you gave them.

The second is to *keep `LOCKING.md` up to date*. Every future change that touches shared state updates the document. A driver whose concurrency story drifts away from its documentation is a driver that accumulates subtle bugs.

The third is to *trust the primitives*. The kernel's mutex, atomic, and sleep primitives are the result of decades of engineering and are extensively tested. The rules feel arbitrary at first; they are not. Learn the rules, apply them consistently, and the kernel does the hard work for you.



## Chapter 11 Reference Material

The sections that follow are reference material for this chapter. They are not part of the main teaching sequence; they are here because you will return to them every time you need to recall a specific primitive, verify an invariant, or revisit a concept the chapter introduced briefly. Read them in order the first time, then come back to individual sections as needed.

The reference sections are named rather than lettered, to avoid confusion with the book's top-level appendices A through E. Each section stands on its own.

## Reference: Building and Booting a Debug Kernel

Several labs in this chapter assume a kernel built with `INVARIANTS` and `WITNESS`. On a FreeBSD 14.3 release system, the stock `GENERIC` kernel does not include these options, because they carry a runtime cost unsuitable for production. This walkthrough shows how to build and install a debug kernel that does.

### Step 1: Prepare a Custom Configuration

Make a copy of the generic configuration so you do not modify the original:

```sh
# cd /usr/src/sys/amd64/conf
# cp GENERIC MYFIRSTDEBUG
```

Edit `MYFIRSTDEBUG` and add the debug options if they are not already present:

```text
ident           MYFIRSTDEBUG
options         INVARIANTS
options         INVARIANT_SUPPORT
options         WITNESS
options         WITNESS_SKIPSPIN
options         DDB
options         KDB
options         KDB_UNATTENDED
```

`DDB` enables the in-kernel debugger, which you can enter on a panic to inspect state. `KDB` is the kernel debugger framework. `KDB_UNATTENDED` causes the system to reboot after a panic rather than waiting for a human at the console, which is the right setting for a headless lab machine.

### Step 2: Build the Kernel

```sh
# cd /usr/src
# make buildkernel KERNCONF=MYFIRSTDEBUG -j 4
```

The `-j 4` parallelizes across 4 cores. Adjust to your machine.

On a reasonably fast machine, the build takes 15 to 30 minutes. On a slow machine, it can take an hour or more. This is a one-time cost; incremental rebuilds are much faster.

### Step 3: Install and Reboot

```sh
# make installkernel KERNCONF=MYFIRSTDEBUG
# shutdown -r now
```

The install places the new kernel in `/boot/kernel/` and the old one in `/boot/kernel.old/`. If the new kernel panics on boot, you can boot from `/boot/kernel.old` at the loader prompt.

### Step 4: Confirm the Boot

After reboot, confirm the debug options are active:

```sh
$ sysctl kern.ident
kern.ident: MYFIRSTDEBUG
```

You are now running a debug kernel. Your driver's `KASSERT` calls will fire on failures. `WITNESS` warnings will appear in `dmesg`.

### Step 5: Rebuild the Driver

Modules must be rebuilt against the running kernel's source:

```sh
$ cd your-driver-directory
$ make clean && make
```

Load as usual:

```sh
# kldload ./myfirst.ko
```

### Reverting to the Production Kernel

If you want to go back to the non-debug kernel:

```sh
# shutdown -r now
```

At the loader prompt, type `unload` then `boot /boot/kernel.old/kernel` (or rename the directories as the FreeBSD handbook describes).

The debug kernel is slower than the production kernel. Do not run production workloads under it. Do run every driver test under it.



## Reference: Further Reading on FreeBSD Concurrency

For readers who want to go deeper than this chapter covers, here are pointers to the canonical sources:

### Manual Pages

- `mutex(9)`: the definitive reference for the mutex API.
- `locking(9)`: an overview of FreeBSD's locking primitives.
- `lock(9)`: the common lock-object infrastructure.
- `atomic(9)`: the atomic operation API.
- `counter(9)`: the per-CPU counter API.
- `msleep(9)` and `mtx_sleep(9)`: the sleep-with-interlock primitives.
- `condvar(9)`: condition variables (Chapter 12 material).
- `sx(9)`: sleepable shared/exclusive locks (Chapter 12 material).
- `rwlock(9)`: reader/writer locks (Chapter 12 material).
- `witness(4)`: the WITNESS lock-order checker.

### Source Files

- `/usr/src/sys/kern/kern_mutex.c`: the mutex implementation.
- `/usr/src/sys/kern/subr_sleepqueue.c`: the sleep queue machinery.
- `/usr/src/sys/kern/subr_witness.c`: the WITNESS implementation.
- `/usr/src/sys/sys/mutex.h`: the mutex header with flag definitions.
- `/usr/src/sys/sys/_mutex.h`: the mutex structure.
- `/usr/src/sys/sys/lock.h`: common lock-object declarations.
- `/usr/src/sys/sys/atomic_common.h` and `/usr/src/sys/amd64/include/atomic.h`: atomic primitives.
- `/usr/src/sys/sys/counter.h`: the counter(9) API header.

### External Material

The FreeBSD handbook covers kernel programming at an overview level. For more depth, *The Design and Implementation of the FreeBSD Operating System* (McKusick et al.) is the canonical textbook. For concurrency theory applicable to any OS, *The Art of Multiprocessor Programming* (Herlihy and Shavit) is excellent. Neither is required reading for our purposes, but both are useful references.



## Reference: A Chapter 11 Self-Assessment

Before turning to Chapter 12, use this rubric to confirm you have internalised the Chapter 11 material. Every question should be answerable without re-reading the chapter. If any question is hard, return to the relevant section.

### Conceptual Questions

1. **Name the four failure modes of unsynchronised shared state.** Data corruption, lost updates, torn values, inconsistent composite state.

2. **What is a race condition?** Overlapping access to shared state, at least one write, correctness that depends on the interleaving.

3. **Why does `mtx_sleep` take the mutex as an interlock argument, rather than the caller unlocking then sleeping?** Because unlocking then sleeping has a lost-wakeup window: a concurrent `wakeup` could fire after the unlock but before the sleep, be delivered to nobody, and leave the sleeper waiting forever.

4. **Why is it illegal to hold a non-sleepable mutex across a call to `uiomove`?** Because `uiomove` may sleep on a user-space page fault, and sleeping with a non-sleepable lock held is forbidden by the kernel (detected by `WITNESS` on debug kernels, undefined on production kernels).

5. **What is the difference between `MTX_DEF` and `MTX_SPIN`?** `MTX_DEF` sleeps when contended; `MTX_SPIN` busy-waits and disables interrupts. Use `MTX_DEF` for code in thread context; use `MTX_SPIN` only when sleeping is forbidden (for example, inside hardware interrupt handlers).

6. **When is an atomic operation sufficient to replace a mutex?** When the shared state is a single word-sized field and the operation does not compose with other fields or require blocking.

7. **What does `WITNESS` catch that single-threaded testing does not?** Lock-order reversals, sleeping with the wrong lock held, releasing an unowned lock, recursive acquisition of a non-recursive lock.

8. **Why is the per-CPU counter (counter(9)) faster than a mutex-protected counter?** Because per-CPU counters do not contend across CPUs. Each CPU updates its own cache line; summing happens only on read.

### Code Reading Questions

Open your Chapter 11 driver source and answer:

1. Every call to `mtx_lock(&sc->mtx)` should be followed eventually by exactly one call to `mtx_unlock(&sc->mtx)` on every path. Verify this by inspection.

2. Every call to `mtx_sleep(&chan, &sc->mtx, ...)` should have a matching `wakeup(&chan)` somewhere in the driver. Find them.

3. The `mtx_assert(&sc->mtx, MA_OWNED)` calls are asserting that the mutex is held. Every caller of those helpers must hold the mutex. Verify by inspection.

4. The detach path sets `sc->is_attached = 0` before calling `wakeup(&sc->cb)`. Why is the order important?

5. The I/O handlers release `sc->mtx` before calling `uiomove`. Why?

6. The `selwakeup` call is placed outside the mutex. Why?

### Hands-On Questions

1. Load the Chapter 11 driver on a `WITNESS`-enabled kernel and run the test kit. Are there any warnings in `dmesg`? If yes, investigate; if no, consider the driver verified.

2. Introduce a deliberate bug: comment out one `wakeup(&sc->cb)` call. Run the test kit. What happens?

3. Revert the bug. Introduce a different deliberate bug: comment out one `mtx_unlock` call and replace it with `mtx_unlock` after the `uiomove` call. Run on `WITNESS`. What does `WITNESS` say?

4. Run `mp_stress` for 60 seconds. Does the byte count converge?

5. Run `producer_consumer` 100 times in a loop. Every run should pass. Does it?

If all five hands-on questions pass, your Chapter 11 work is solid. You are ready for Chapter 12.



## Reference: A Closer Look at Sleep Queues

This reference section explains, at a level appropriate for a beginner with curiosity, what happens inside the kernel when a thread calls `mtx_sleep` or `wakeup`. You do not need this material to write a correct driver; you have been doing that for several chapters. The section is here because, once you have seen the primitives work, you may want to know *how* they work. The knowledge pays off when you read other drivers and when you debug rare concurrency issues.

### The Sleep Queue

A **sleep queue** is a kernel data structure that tracks threads waiting for a condition. Every active wait corresponds to exactly one sleep queue. When a thread calls `mtx_sleep(chan, mtx, pri, wmesg, timo)`, the kernel looks up (or creates) the sleep queue associated with `chan` (the wait channel) and adds the thread to it. When `wakeup(chan)` is called, the kernel looks up the queue and wakes every thread on it.

The data structure itself is defined in `/usr/src/sys/kern/subr_sleepqueue.c`. A hash table keyed on the channel address maps channels to queues. Each queue has a spin lock protecting its list of waiters. The spin lock is internal to the sleep-queue machinery; you never see it directly.

Why a hash table? Because a channel can be any pointer value, and the kernel may be serving thousands of simultaneous waits. A direct map (one queue per channel) would be too sparse; a single global list would contend too much. The hash table splits the load across many buckets, keeping lookups fast.

### The Atomicity of mtx_sleep

Here is what `mtx_sleep(chan, mtx, pri, wmesg, timo)` actually does, at a high level:

1. Take the sleep-queue spin lock for `chan`.
2. Add the current thread to the sleep queue under that lock.
3. Release `mtx` (the outer mutex the caller is holding).
4. Call the scheduler, which picks another thread to run.
5. When awakened, the scheduler restores this thread. Control returns inside `mtx_sleep`.
6. Release the sleep-queue spin lock.
7. Reacquire `mtx`.
8. Return to the caller.

The key point is that steps 1 and 2 happen before step 3. Once the thread is on the sleep queue, any subsequent `wakeup(chan)` will remove it. If `wakeup(chan)` fires between step 3 and step 4, it removes the thread from the queue and marks it runnable; step 4 (calling the scheduler) will still happen but will find the thread immediately runnable, so the sleep is zero-length.

The window that would have been "released the mutex, not yet on the sleep queue" does not exist. The sleep queue entry happens *first*, while the mutex is still held; then the mutex is released *while the sleep-queue lock is held*. No wakeup can slip through.

This is the race that the atomic drop-and-sleep prevents, visible at the source level.

### The wakeup Path

`wakeup(chan)` does the following:

1. Take the sleep-queue spin lock for `chan`.
2. Remove every thread on the queue.
3. Mark each removed thread as runnable.
4. Release the sleep-queue spin lock.
5. If any of the removed threads had higher priority than the current thread, call the scheduler to preempt.

The removed threads do not immediately run; they are merely runnable. The scheduler will pick them up in due course. If none has a higher priority than the caller, the caller continues to run; if one does, the caller yields.

`wakeup_one(chan)` is the same except that it removes only one thread (the highest-priority one, with FIFO among equal priorities). This is preferred when only one thread can usefully consume the signal. For a single-consumer buffer, `wakeup_one` avoids the thundering-herd problem; for a signal that can benefit multiple consumers, `wakeup` is appropriate.

For our driver, either would work. We use `wakeup` because both a reader and a writer may be waiting on the same channel and both need to be notified if the buffer state changes.

### Priority Propagation Inside mtx_sleep

Section 5 introduced priority propagation as the kernel's defence against priority inversion. The mechanism lives in the mutex code itself: when `mtx_sleep` adds the calling thread to the sleep queue, it inspects the owner of the mutex (stored as a thread pointer inside the mutex word). If the waiting thread's priority is higher than the owner's, the owner's priority is boosted. The boost persists until the owner releases the mutex, at which point the owner's original priority is restored and the waiter acquires.

You can observe this effect live on a running system with `top -SH` during a contested workload: the priority of the thread holding a contested lock dances around as different waiters arrive and leave. It is one of the kernel services you benefit from silently; the boost is automatic and correct, and you do not have to opt in.

### Adaptive Spinning Revisited

Section 5 introduced adaptive spinning as the optimization that lets `mtx_lock` avoid going to sleep when the holder is about to release. The sleep-queue machinery is where the decision is made. At its core, the slow path does roughly:

```text
while (!try_acquire()) {
        if (holder is running on another CPU)
                spin briefly;
        else
                enqueue on the sleep queue and yield;
}
```

The "holder is running on another CPU" check reads the mutex word to get the owner pointer, then asks the scheduler whether that thread is currently on-CPU. If yes, the mutex is likely to be released within microseconds and a short spin beats the cost of a context switch. If no (the holder is itself asleep or has been descheduled), spinning is pointless and the waiter joins the sleep queue immediately. The threshold and exact spin count are kernel tunables; their precise values do not matter for the driver writer, as long as you trust that `mtx_lock` is fast in the common case and falls gracefully to sleep when it has to.

### The Priority Argument

`mtx_sleep` takes a priority argument. In the Chapter 10 code, we passed `PCATCH` (allow signal interruption) with no explicit priority. The default priority, in FreeBSD, is whatever the calling thread is currently at; `PCATCH` alone does not change the priority.

Sometimes you want to sleep at a specific priority. The argument takes the form `PRIORITY | FLAGS`, where `PRIORITY` is a number (lower is better) and `FLAGS` include `PCATCH`. For our driver, the default priority is fine. Specialised drivers might pass `PUSER | PCATCH` to ensure the thread sleeps at a priority appropriate for user-visible work; real-time drivers might pass a lower (better) priority.

This is a detail you can safely ignore until a specific driver needs it. The default is the right answer most of the time.

### Wait Message (wmesg) Conventions

The `wmesg` argument to `mtx_sleep` is a short string that appears in `ps`, `procstat`, and similar tools. Conventions:

- Five to seven characters. Longer strings are truncated.
- Lowercase.
- Indicate both the subsystem and the operation.
- `myfrd` = "myfirst read". `myfwr` = "myfirst write". `tunet` = "tun network".

A good `wmesg` lets a developer scanning a `ps -AxH` output immediately know what a sleeping thread is waiting for. The few seconds it takes to invent a good name pay off the first time somebody needs to understand a hung system.

### Sleep with Timeouts

The `timo` argument to `mtx_sleep` is the maximum wait time in ticks. Zero means "indefinite"; a positive value means "wake after this many ticks even if no one called `wakeup`".

When the timeout fires, `mtx_sleep` returns `EWOULDBLOCK`. The caller is expected to check the return value and decide what to do.

Timeouts are how you implement bounded blocking. Chapter 12 covers `msleep_sbt(9)`, which takes an `sbintime_t` deadline instead of an integer tick count and is more convenient for sub-millisecond precision. For Chapter 11, the plain tick-based timeout is sufficient.

Example usage, for a driver that wants a 5-second maximum wait:

```c
error = mtx_sleep(&sc->cb, &sc->mtx, PCATCH, "myfrd", hz * 5);
if (error == EWOULDBLOCK) {
        /* Timed out; return EAGAIN or retry. */
}
```

The `hz` constant is the kernel's tick rate (typically 1000 on FreeBSD 14.3, configurable via `kern.hz`). Multiplying by 5 gives 5 seconds. This is a handy technique for a driver that wants progress guarantees.



## Reference: Reading kern_mutex.c

The source for FreeBSD's mutex implementation lives in `/usr/src/sys/kern/kern_mutex.c`. This reference section walks through the most interesting parts at a level appropriate for a driver writer who wants to know what happens when they call `mtx_lock`.

You do not need to understand every line. You do need to be able to open the file and recognize the shape of what is happening.

### The mutex Structure

A mutex is a small structure. Its definition is in `/usr/src/sys/sys/_mutex.h`:

```c
struct mtx {
        struct lock_object      lock_object;
        volatile uintptr_t      mtx_lock;
};
```

`lock_object` is the common header used by every lock class (mutexes, sx locks, rw locks, lockmgr locks, rmlocks). It holds the name, the lock flags, and WITNESS tracking state.

`mtx_lock` is the actual lock word. Its value encodes the state:

- `0`: unlocked.
- A thread pointer: the ID of the thread holding the lock.
- Thread pointer with low bits set: lock is held with modifiers (`MTX_CONTESTED`, `MTX_RECURSED`, etc.).

Acquiring the mutex is, at its core, a compare-and-swap: change `mtx_lock` from `0` (unlocked) to `curthread` (the calling thread's pointer).

### The Fast Path of mtx_lock

Open `kern_mutex.c` and find `__mtx_lock_flags`. You will see something like:

```c
void
__mtx_lock_flags(volatile uintptr_t *c, int opts, const char *file, int line)
{
        struct mtx *m;
        uintptr_t tid, v;

        m = mtxlock2mtx(c);
        /* ... WITNESS setup ... */

        tid = (uintptr_t)curthread;
        v = MTX_UNOWNED;
        if (!_mtx_obtain_lock_fetch(m, &v, tid))
                _mtx_lock_sleep(m, v, opts, file, line);
        /* ... lock acquired; lock-profiling bookkeeping ... */
}
```

The critical operation is `_mtx_obtain_lock_fetch`, which is a compare-and-swap. It tries to change `mtx->mtx_lock` from `MTX_UNOWNED` (zero) to `tid` (the current thread's pointer). If it succeeds, we hold the mutex, and the function returns without further work. If it fails, we go to `_mtx_lock_sleep`.

The fast path is therefore: one compare-and-swap plus some WITNESS bookkeeping. On an uncontested mutex, `mtx_lock` costs essentially one atomic operation.

### The Slow Path

`_mtx_lock_sleep` is the slow path. It handles the case where the mutex is already held by another thread. The function is a few hundred lines; the critical parts are:

```c
for (;;) {
        /* Try to acquire. */
        if (_mtx_obtain_lock_fetch(m, &v, tid))
                break;

        /* Adaptive spin if holder is running. */
        if (owner_running(v)) {
                for (i = 0; i < spin_count; i++) {
                        if (_mtx_obtain_lock_fetch(m, &v, tid))
                                goto acquired;
                }
        }

        /* Go to sleep. */
        enqueue_on_sleep_queue(m);
        schedule_out();
        /* When we resume, loop back and try again. */
}
acquired:
        /* ... priority propagation bookkeeping ... */
```

The structure is: try the compare-and-swap, spin if the holder is running, otherwise sleep. Wake up and retry. Eventually acquire.

Several pieces of the real code complicate this skeleton: handling of the `MTX_CONTESTED` flag, priority propagation, lock profiling, witness tracking, and special cases for recursion. Don't worry about them on a first read; the structure above is the essence.

### The Unlock Path

`__mtx_unlock_flags` is the release. Its fast path:

```c
void
__mtx_unlock_flags(volatile uintptr_t *c, int opts, const char *file, int line)
{
        struct mtx *m;
        uintptr_t tid;

        m = mtxlock2mtx(c);
        tid = (uintptr_t)curthread;

        if (!_mtx_release_lock(m, tid))
                _mtx_unlock_sleep(m, opts, file, line);
}
```

`_mtx_release_lock` tries to atomically change `mtx->mtx_lock` from `tid` (current thread) to `MTX_UNOWNED`. If it succeeds, the mutex is released cleanly. If it fails, there were complications (the `MTX_CONTESTED` flag was set because waiters had arrived), and `_mtx_unlock_sleep` handles them.

The common case: one atomic store. The uncommon case: dequeue waiters and wake one.

### The Takeaway

Reading `kern_mutex.c` once, even briefly, gives you a useful intuition. The mutex is not magic. It is a compare-and-swap with a fallback to a sleep queue. Everything else is bookkeeping and optimization.

The primitives you use (`mtx_lock`, `mtx_unlock`) are thin wrappers around these internals. Understanding them means you can reason about their cost, their guarantees, and their limits. That is the payoff.



## Reference: A Mini-Glossary of Concurrency Vocabulary

Terminology matters in concurrency, more than in most subjects, because people who disagree about terminology often end up disagreeing about whether their code is correct. A short glossary of the terms used in this chapter:

**Atomic operation**: a memory operation that is indivisible from the perspective of other CPUs. It completes as a single transaction; no other CPU can observe a partial result.

**Barrier / fence**: a point in the code past which the compiler and CPU are not allowed to move specific kinds of memory accesses. Barriers come in acquire, release, and full forms.

**Critical section**: a contiguous region of code that accesses shared state and must execute without interference from concurrent threads.

**Deadlock**: a situation in which two or more threads are each waiting for a resource held by another, with none able to make progress.

**Invariant**: a property of a data structure that is true outside of critical sections. Code inside a critical section may temporarily violate the invariant; code outside must not.

**Livelock**: a situation in which threads are making progress in the sense of executing instructions, but the useful state of the system does not advance. Typically caused by spinning retries that all fail.

**Lock-free**: describing an algorithm or data structure that avoids mutual-exclusion primitives, typically relying on atomics. Lock-free algorithms guarantee that at least one thread always makes progress; they do not necessarily prevent starvation of specific threads.

**Memory ordering**: the rules governing when a write on one CPU becomes visible to reads on other CPUs. Different architectures have different models (strongly ordered, weakly ordered, release-acquire).

**Mutual exclusion (mutex)**: a synchronization primitive ensuring that only one thread at a time can execute a protected region.

**Priority inversion**: a situation in which a low-priority thread holds a resource that a high-priority thread needs, and a medium-priority thread preempts the low-priority holder, effectively blocking the high-priority waiter.

**Priority propagation (inheritance)**: the kernel's mechanism for solving priority inversion by temporarily raising a low-priority holder's priority to match its highest-priority waiter.

**Race condition**: a situation in which the correctness of a program depends on the exact interleaving of concurrent accesses to shared state, and at least one access is a write.

**Sleep mutex (`MTX_DEF`)**: a mutex that puts contending threads to sleep. Legal to use in contexts where sleeping is allowed.

**Spin mutex (`MTX_SPIN`)**: a mutex that causes contending threads to spin in a busy loop. Required in contexts where sleeping is forbidden, such as hardware interrupt handlers.

**Starvation**: a situation in which a thread is runnable but never actually runs, because other threads are always scheduled ahead of it. Priority inversion is a specific cause of starvation.

**Wait-free**: a stricter property than lock-free. Every thread is guaranteed to make progress within a bounded number of steps, regardless of what other threads do.

**WITNESS**: FreeBSD's lock-order and locking-discipline checker. A kernel option that tracks lock acquisitions and warns on violations.



## Reference: Concurrency Patterns in Real FreeBSD Drivers

Before Chapter 12 introduces additional synchronization primitives, it helps to survey how concurrency is handled in real drivers in `/usr/src/sys/dev/`. The patterns below are the ones you will see most often. Each shows that the tools you have learned (a mutex, a sleep channel, selinfo) are the same tools that ship with FreeBSD.

### Pattern: Per-Driver Softc Mutex

Nearly every character-device driver in the tree uses a single mutex per softc. Examples:

- `/usr/src/sys/dev/evdev/evdev_private.h` declares `ec_buffer_mtx` for the per-client state.
- `/usr/src/sys/dev/random/randomdev.c` uses `sysctl_lock` for its private configuration.
- `/usr/src/sys/dev/null/null.c` does not use a mutex, because its cbuf is effectively stateless; this is the exception that confirms the rule.

The per-softc mutex pattern is not novel, not sophisticated, and not optional. It is the backbone of driver concurrency in FreeBSD. Our `myfirst` driver follows it because every small driver follows it.

### Pattern: Sleep Channel on a softc Field

The convention for the `chan` argument to `mtx_sleep` is to use the address of a softc field related to the wait condition. Examples:

- `evdev_read` in `/usr/src/sys/dev/evdev/cdev.c` sleeps on the per-client structure (via `mtx_sleep(client, ...)`) and is woken when new events arrive.
- Our `myfirst_read` sleeps on `&sc->cb` and wakes correspondingly.

The address can be anything; what matters is that sleepers and wakers use the same pointer. Using a struct field's address has the benefit that the channel is visible in source and self-documenting: a reader of the code knows what the thread is waiting for.

### Pattern: selinfo for poll() Support

Every driver that supports `poll(2)` or `select(2)` has at least one `struct selinfo`, sometimes two (for read and write readiness). The same two calls appear:

- `selrecord(td, &si)` inside the d_poll handler.
- `selwakeup(&si)` when the readiness state changes.

This is exactly what `myfirst` does. The evdev driver does it. The tty drivers do it. It is the standard form.

### Pattern: Bounce Buffer with uiomove

Many drivers that have wrap-around buffers use the bounce-buffer pattern we discussed in Chapter 10: copy bytes from the ring into a stack-local scratch, drop the lock, uiomove the scratch to user space, reacquire the lock. Examples:

- `evdev_read` copies a single event structure into a scratch variable before calling `uiomove`.
- `ucom_put_data` in `/usr/src/sys/dev/usb/serial/usb_serial.c` has a similar shape for its circular buffer.
- Our `myfirst_read` has the same shape for the cbuf.

This is the idiomatic answer to "how do I do uiomove from a wrap-around buffer while holding a mutex?". The bounce is small (often on the stack, sometimes a dedicated small buffer), and the lock is dropped around the uiomove.

### Pattern: Counter(9) for Statistics

Modern drivers use `counter(9)` for statistics counters. Examples:

- `/usr/src/sys/net/if.c` uses `if_ierrors`, `if_oerrors`, and many other counters as `counter_u64_t`.
- Many of the network drivers (ixgbe, mlx5, etc.) use counter(9) for their per-queue statistics.

Our `myfirst` migration to `counter(9)` puts us in line with this modern pattern. Older drivers that still use plain atomic counters typically predate the `counter(9)` API; newer code uses `counter(9)`.

### Pattern: Detach with Outstanding Users

A recurring challenge is detach when user processes still have the device open. Two common approaches:

**Refuse detach while in use.** Our driver does this: detach returns `EBUSY` if `active_fhs > 0`. Simple, safe, but means the module cannot be unloaded while anyone is using it.

**Forced detach with destroy_dev_drain.** The kernel provides `destroy_dev_drain(9)`, which waits for all handlers to return and prevents new ones from starting. Combined with a "this device is going away" flag in the softc, this allows forced detach with graceful completion of in-flight operations. Chapter 12 and beyond cover this in more depth.

For `myfirst`, the simpler "refuse detach" approach is correct. A production driver that must support hot-unplug (USB, for example) uses the more complex approach.

### Pattern: WITNESS-Documented Lock Order

Drivers with more than one lock document the order. Example:

```c
/*
 * Locks in this driver, in acquisition order:
 *   sc->sc_mtx
 *   sc->sc_listmtx
 *
 * A thread holding sc->sc_mtx may acquire sc->sc_listmtx, but
 * not the reverse.  See sc_add_entry() for the canonical example.
 */
```

WITNESS validates this ordering at runtime; the comment makes it readable for humans. Our driver has one lock and no ordering rules, but as soon as we add a second (Chapter 12 may introduce one), we will add this kind of documentation.

### Pattern: Assertions Everywhere

Real drivers are littered with `KASSERT` and `mtx_assert`. Examples:

- `if_alloc` in `/usr/src/sys/net/if.c` asserts invariants on input structures.
- GEOM code (in `/usr/src/sys/geom/`) has `g_topology_assert` and similar in most functions.
- Network drivers often have `NET_EPOCH_ASSERT()` to confirm the caller is in the right context.

Assertions are cheap in production and life-saving in development. Our Chapter 11 work added several; future chapters will add more.

### What Real Drivers Do That Ours Does Not

Three patterns appear in real drivers that our driver does not yet use:

**Finer-grained locking.** A network driver might have a lock per queue; a storage driver might have a lock per request. Our driver has one lock for everything. For small drivers this is correct; for drivers with hot paths on many cores, it is not enough. Chapter 12's `sx` lock and Chapter 15's discussion of RCU-style patterns cover the finer-grained cases.

**Lock-free hot paths.** `buf_ring(9)` gives multi-producer queues with either single-consumer or multi-consumer dequeue, with no locks on the hot path. `epoch(9)` lets readers proceed lock-free. These are optimizations for specific workloads. A beginner driver should not use them first; the justification should be a measured bottleneck, not a speculative one.

**Hierarchical locking with multiple classes.** A real driver might use a spin mutex for interrupt context, a sleep mutex for normal work, and an sx lock for configuration. Each has its place; the interactions are governed by strict ordering rules. We will introduce these in Chapter 12 and revisit them in later chapters when specific drivers need them.

### The Takeaway

The patterns in `/usr/src/sys/dev/` are not exotic. They are the ones we have been building. An `evdev_client` with a mutex, a sleep channel, and a selinfo is structurally the same as a `myfirst_softc` with the same three pieces. Reading a few of these drivers with the tools Chapter 11 has built should feel almost familiar. When something looks strange, it is usually a specialisation (for example, kqueue filter registration) that future chapters cover.

This is the compounding payoff of the book: each chapter's patterns make a larger fraction of the real source readable.



## Reference: Common Mutex Idioms

This is a quick-lookup collection of the idioms you will use most often in FreeBSD driver code.

### Basic Critical Section

```c
mtx_lock(&sc->mtx);
/* access shared state */
mtx_unlock(&sc->mtx);
```

### Assert Held

```c
mtx_assert(&sc->mtx, MA_OWNED);
```

Put at the top of every helper that assumes the mutex is held. Compiles to nothing on non-`INVARIANTS` kernels; panics on `INVARIANTS` kernels if the mutex is not held.

### Try-Lock

```c
if (mtx_trylock(&sc->mtx)) {
        /* got it */
        mtx_unlock(&sc->mtx);
} else {
        /* someone else has it; back off */
}
```

Use when you want to avoid blocking. Useful in timer callbacks that should not contend with I/O handlers; if the lock is taken, skip this tick and retry next time.

### Sleep Waiting for a Condition

```c
mtx_lock(&sc->mtx);
while (!condition)
        mtx_sleep(&chan, &sc->mtx, PCATCH, "wmesg", 0);
/* condition is true now; do work */
mtx_unlock(&sc->mtx);
```

The `while` loop is essential. Spurious wake-ups are permitted and common; the thread must re-check the condition after every return from `mtx_sleep`.

### Wake Sleepers

```c
wakeup(&chan);              /* wake all */
wakeup_one(&chan);          /* wake highest-priority one */
```

Call without holding the mutex. The kernel handles ordering.

### Sleep with Timeout

```c
error = mtx_sleep(&chan, &sc->mtx, PCATCH, "wmesg", hz * 5);
if (error == EWOULDBLOCK) {
        /* timed out */
}
```

Bound the wait to 5 seconds. Convenient for heartbeats and deadline-driven operations.

### Drop and Reacquire Around a Potentially-Sleeping Call

```c
mtx_lock(&sc->mtx);
/* snapshot what we need */
snap = sc->field;
mtx_unlock(&sc->mtx);

/* do the sleeping call */
error = uiomove(buf, snap, uio);

mtx_lock(&sc->mtx);
/* update shared state */
sc->other_field = /* ... */;
mtx_unlock(&sc->mtx);
```

This is the pattern our I/O handlers use. The mutex is never held across a sleeping call.

### Atomic-Instead-of-Lock for Simple Counters

```c
counter_u64_add(sc->bytes_read, got);
```

Faster and more scalable than `mtx_lock; sc->bytes_read += got; mtx_unlock`.

### Check and Set Atomically

```c
if (atomic_cmpset_int(&sc->flag, 0, 1)) {
        /* we set it from 0 to 1; we are the "first" */
}
```

No mutex needed. The compare-and-swap itself is atomic.



## Looking Ahead: Bridge to Chapter 12

Chapter 12 is titled *Synchronization Mechanisms*. Its scope is the rest of the FreeBSD synchronization toolkit: condition variables, sleepable shared-exclusive locks, read/write locks, timed waits, and the deeper techniques for debugging deadlocks. Everything in Chapter 12 builds on the mutex foundation Chapter 11 laid down.

The bridge is three specific observations from Chapter 11's work.

First, you already use a "channel" (`&sc->cb`) with `mtx_sleep` and `wakeup`. Chapter 12 introduces **condition variables** (`cv(9)`), which are a named, structured alternative to the anonymous-channel pattern. You will see that `cv_wait_sig(&cb_has_data, &sc->mtx)` is often clearer than `mtx_sleep(&sc->cb, &sc->mtx, PCATCH, "myfrd", 0)`, especially when there are multiple conditions the thread might wait on.

Second, your driver's one mutex serializes everything, including reads that could theoretically proceed in parallel. Chapter 12 introduces `sx(9)` sleepable shared-exclusive locks, which allow multiple readers to hold the lock simultaneously while serializing writers. You will reason about when that design is appropriate (read-heavy workloads, configuration state) and when it is overkill (small critical sections where the mutex is already fine).

Third, your blocking path blocks indefinitely. Chapter 12 introduces **timed waits**: `mtx_sleep` with a nonzero `timo`, `cv_timedwait`, and `sx_sleep` with timeouts. You will learn how to bound the waiting time so a user can press Ctrl-C and have the driver respond within a sensible interval.

Specific topics Chapter 12 will cover include:

- The `cv_wait`, `cv_signal`, `cv_broadcast`, and `cv_timedwait` primitives.
- The `sx(9)` API: `sx_init`, `sx_slock`, `sx_xlock`, `sx_sunlock`, `sx_xunlock`, `sx_downgrade`, `sx_try_upgrade`.
- Reader/writer locks (`rw(9)`) as a spin-mutex sibling of `sx`.
- Timeout handling and the `sbintime_t` family of time primitives.
- Deadlock debugging in depth, including the `WITNESS` lock-order list.
- Lock-order design patterns and the `LORD_*` conventions.
- A concrete application: upgrading parts of the driver to use `sx` and `cv` where appropriate.

You do not need to read ahead. Chapter 11's material is sufficient preparation. Bring your Stage 3 or Stage 5 driver, your test kit, and your `WITNESS`-enabled kernel. Chapter 12 starts where Chapter 11 ended.

A small closing reflection. You have just done something unusual. Most driver tutorials teach mutex usage as a formula: put a `mtx_lock` here and a `mtx_unlock` there, and the driver will work. You have done the harder thing: you have understood what the formula means, when it is necessary, why it is sufficient, and how to verify it. That understanding is what separates a driver writer from a driver author. Every chapter from here forward builds on it.

Take a moment. Appreciate how far you have come from the opening "Hello, kernel!" of Chapter 7. Then turn the page.
