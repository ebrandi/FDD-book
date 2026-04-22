---
title: "Synchronization Mechanisms"
description: "Naming the channels you wait on, taking lock once and reading from many threads, bounding indefinite blocks, and turning Chapter 11's mutex into a synchronization design you can defend."
partNumber: 3
partName: "Concurrency and Synchronization"
chapter: 12
lastUpdated: "2026-04-18"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "TBD"
estimatedReadTime: 195
---

# Synchronization Mechanisms

## Reader Guidance & Outcomes

Chapter 11 ended with a driver that was, for the first time in this book, *verifiably* concurrent. You had a single mutex protecting the circular buffer, atomic counters that scaled to many cores, `WITNESS` and `INVARIANTS` watching every lock acquisition, a stress-test kit that ran on a debug kernel, and a `LOCKING.md` document that any future maintainer (including future you) could read to understand the concurrency story. That was real progress. It was not, however, the end of the story.

The synchronization toolkit FreeBSD gives you is much larger than the single primitive Chapter 11 introduced. The mutex you used is the right answer for many situations. It is also the wrong answer for several others. A read-mostly configuration table that twenty threads scan ten thousand times per second wants something other than a mutex serializing the reads. A blocking read that should respond to Ctrl-C within milliseconds wants something other than `mtx_sleep(9)` with no timeout. A coordinated wake-up across a dozen waiters, one per condition, wants something more expressive than an anonymous channel pointer like `&sc->cb`. The kernel has primitives for each of these, and Chapter 12 is where we meet them.

This chapter does for the rest of the synchronization toolkit what Chapter 11 did for the mutex. We introduce each primitive with motivation first, build the mental model, ground it in real FreeBSD source, apply it to the running `myfirst` driver, and verify the result against a `WITNESS` kernel. By the end you will be able to read a real driver and recognise, by the choice of primitive alone, what its author was trying to express. You will also be able to make those choices yourself, in your own driver, without reaching for the wrong tool out of habit.

### Why This Chapter Earns Its Own Place

It would be possible, in principle, to stop with Chapter 11. A mutex, an atomic counter, and `mtx_sleep` cover the simple cases. Many small drivers in the FreeBSD tree do not use anything else.

The trouble is that "many small drivers" is not where most of the bugs live. The drivers people maintain longest are the ones that grew. A USB device driver started small, then acquired a control channel, then a configuration table that user space could change at runtime, then a separate event queue with its own waiters. Every one of those additions exposed the limits of "one mutex protects everything". A driver writer who only knows the mutex ends up either misusing it (a held-too-long mutex blocks the whole subsystem) or working around it (a tangle of busy-wait loops, racy retries, and global flags that "should not happen but sometimes do"). The synchronization primitives this chapter teaches exist precisely to keep those workarounds out of the code.

Each primitive is a different shape of agreement between threads. A mutex says *only one of us at a time*. A condition variable says *I will wait for a specific change you will tell me about*. A shared/exclusive lock says *many of us may read; only one of us may write*. A timed sleep says *and please give up if it is taking too long*. A driver that uses each tool for what it is good at reads cleanly, behaves predictably, and remains comprehensible long after the original author has stopped looking at it. A driver that uses one tool for everything either suffers in performance or hides bugs in places no one is looking.

This chapter is therefore a vocabulary chapter as much as it is a mechanics chapter. We do introduce the APIs and we do walk through code. The deeper goal is to give you the words for what you are trying to say.

### Where Chapter 11 Left the Driver

A quick checkpoint, because Chapter 12 builds directly on top of the Chapter 11 deliverables. If any of the following is missing or feels uncertain, return to Chapter 11 before starting this chapter.

- Your `myfirst` driver compiles cleanly with `WARNS=6`.
- It uses `MYFIRST_LOCK(sc)`, `MYFIRST_UNLOCK(sc)`, and `MYFIRST_ASSERT(sc)` macros that expand to `mtx_lock`, `mtx_unlock`, and `mtx_assert(MA_OWNED)` on the device-wide `sc->mtx` (an `MTX_DEF` sleep mutex).
- The cbuf, the per-descriptor counters, the open count, and the active-descriptor count are all protected by `sc->mtx`.
- The byte counters `sc->bytes_read` and `sc->bytes_written` are `counter_u64_t` per-CPU counters; they do not need the mutex.
- The blocking read and write paths use `mtx_sleep(&sc->cb, &sc->mtx, PCATCH, "myfrd"|"myfwr", 0)` as their wait primitive and `wakeup(&sc->cb)` as the matching wake.
- `INVARIANTS` and `WITNESS` are enabled in your test kernel; you have built and booted it.
- A `LOCKING.md` document accompanies the driver and lists every shared field, every lock, every wait channel, and every "deliberately not locked" decision with a justification.
- The Chapter 11 stress kit (`producer_consumer`, `mp_stress`, `mt_reader`, `lat_tester`) builds and runs cleanly.

That driver is the substrate this chapter starts from. We will not throw it away. We will replace some primitives with better-fitting ones, add a small new subsystem to give the new primitives something to protect, and finish with a driver whose synchronization design is both more capable and easier to read.

### What You Will Learn

When you close this chapter you should be able to:

- Explain what *synchronization* means in a kernel-level sense, and distinguish it from the narrower concept of *mutual exclusion*.
- Map the FreeBSD synchronization toolkit onto a small decision tree: mutex, condition variable, shared/exclusive lock, reader/writer lock, atomic, sleep with timeout.
- Replace anonymous wait channels with named condition variables (`cv(9)`), and explain why that change improves both correctness and readability.
- Use `cv_wait`, `cv_wait_sig`, `cv_wait_unlock`, `cv_signal`, `cv_broadcast`, and `cv_broadcastpri` correctly with respect to their interlock mutex.
- Bound a blocking operation by a timeout using `cv_timedwait_sig` (or `mtx_sleep` with a non-zero `timo` argument), and design the caller's response when the timeout fires.
- Distinguish `EINTR`, `ERESTART`, and `EWOULDBLOCK`, and decide which the driver should return when a wait fails.
- Choose between `sx(9)` (sleepable) and `rw(9)` (spin-based) reader/writer locks, understand the rules each places on the calling context, and apply `sx_init`, `sx_xlock`, `sx_slock`, `sx_xunlock`, `sx_sunlock`, `sx_try_upgrade`, and `sx_downgrade`.
- Design a multi-reader, single-writer arrangement that uses one lock for the data path and another for the configuration path, with a documented lock order and a `WITNESS`-verified absence of inversions.
- Read `WITNESS` warnings precisely enough to identify the exact lock pair and the source line of the violation.
- Use the in-kernel debugger commands `show locks`, `show all locks`, `show witness`, and `show lockchain` to inspect a system that has hung on a synchronization bug.
- Build a stress workload that exercises the driver across descriptors, sysctls, and timed waits at the same time, and read the output of `lockstat(1)` to find the contended primitives.
- Refactor the driver into a shape where the synchronization story is documented, the lock order is explicit, and the version string reflects the new architecture.

That is a substantial list. None of it is optional for a driver that aspires to outlast its author. All of it builds on what Chapter 11 left in your hands.

### What This Chapter Does Not Cover

Several adjacent topics are deliberately deferred:

- **Callouts (`callout(9)`).** Chapter 13 introduces timed work that fires from the kernel's clock infrastructure. We touch the topic here only as a sleep-with-timeout primitive seen from the driver's blocking call; the full callout API and its rules belong in Chapter 13.
- **Taskqueues (`taskqueue(9)`).** Chapter 16 introduces the kernel's deferred-work framework. Several drivers use a taskqueue to decouple the blocking thread from the wake-up signal, but doing so well requires its own chapter.
- **`epoch(9)` and read-mostly lock-free patterns.** Network drivers in particular use `epoch(9)` to let readers proceed without acquiring a lock at all. The mechanism is subtle and is best taught alongside the network driver subsystem in Part 6.
- **Interrupt-context synchronization.** Real hardware interrupt handlers add another layer of constraint on what locks you can hold and which sleep primitives are legal. Chapter 14 introduces interrupt handlers and revisits the synchronization rules from inside that context. For Chapter 12 we stay entirely in process and kernel-thread context.
- **Lockless data structures.** `buf_ring(9)` and friends are effective tools for hot paths, but they reward careful study and require a specific workload to pay back their complexity. Part 6 (Chapter 28) introduces them when a driver in the book actually needs one.
- **Distributed and cross-machine synchronization.** Out of scope. We are a single-host operating system in this book.

Staying inside those lines keeps the chapter focused on what it can teach well. The reader of Chapter 12 should finish with confident control of `cv(9)`, `sx(9)`, and timed waits, and a working sense of where `rw(9)` and `epoch(9)` fit; that confidence is what makes the later chapters readable when they show up.

### Estimated Time Investment

- **Reading only**: about three hours. The new vocabulary (condition variables, shared/exclusive locks, sleepability rules) takes time to absorb even though the API surface is small.
- **Reading plus typing the worked examples**: six to eight hours over two sessions. The driver evolves in four small stages; each stage adds one primitive.
- **Reading plus all labs and challenges**: ten to fourteen hours over three or four sessions, including time for stress runs and `lockstat(1)` analysis.

If you find yourself confused midway through Section 4, that is normal. The shared/exclusive distinction is genuinely new even for readers comfortable with mutexes, and the temptation to use `sx` for the data path is exactly the temptation Section 5 exists to resolve. Stop, re-read Section 4's example, and continue when the model has settled.

### Prerequisites

Before starting this chapter, confirm:

- Your driver source matches the Chapter 11 Stage 3 (counter9) or Stage 5 (KASSERTs) tree. Stage 5 is preferred because the assertions catch new bugs faster.
- Your lab machine is running FreeBSD 14.3 with `/usr/src` on disk and matching the running kernel.
- A debug kernel with `INVARIANTS`, `WITNESS`, `WITNESS_SKIPSPIN`, `DDB`, `KDB`, and `KDB_UNATTENDED` is built, installed, and booting cleanly. The Chapter 11 reference section "Building and Booting a Debug Kernel" has the recipe.
- You read Chapter 11 carefully. The mutex rules, the sleep-with-mutex rule, the lock-order discipline, and the `WITNESS` workflow are all assumed knowledge here.
- You have run the Chapter 11 stress kit at least once and seen it pass.

If any of the above is shaky, fixing it now is a much better investment than pushing through Chapter 12 and trying to debug from a moving foundation.

### How to Get the Most Out of This Chapter

Three habits will pay off quickly.

First, keep `/usr/src/sys/kern/kern_condvar.c`, `/usr/src/sys/kern/kern_sx.c`, and `/usr/src/sys/kern/kern_rwlock.c` bookmarked. Each one is short, well-commented, and the source of authority for what the primitive actually does. Several times in the chapter we will ask you to glance at a specific function. A minute spent there will make the surrounding paragraphs easier to absorb.

> **A note on line numbers.** When we point you at a specific function later in the chapter, open the file and search for the symbol rather than jumping to a numeric line. `_cv_wait_sig` lives in `/usr/src/sys/kern/kern_condvar.c` and `sleepq_signal` in `/usr/src/sys/kern/subr_sleepqueue.c` in the 14.3 tree at time of writing; the names will carry across future point releases, but the line each one sits on will not. The durable reference is always the symbol.

Second, run every code change you make under `WITNESS`. The synchronization primitives we introduce in this chapter have stricter rules than the mutex did. `WITNESS` is the cheapest way to discover that you have broken one. The Chapter 11 reference section "Building and Booting a Debug Kernel" walks through the kernel build if you need it; do not skip it now.

Third, type the driver changes by hand when you can. The companion source under `examples/part-03/ch12-synchronization-mechanisms/` is the canonical version, but the muscle memory of typing `cv_wait_sig(&sc->data_cv, &sc->mtx)` once is worth more than reading it ten times. The chapter shows the changes incrementally; mirror that incremental pace in your own copy of the driver.

### Roadmap Through the Chapter

The sections in order are:

1. What synchronization is in the kernel, and where the various primitives sit in a small decision tree.
2. Condition variables, the cleaner alternative to anonymous wakeup channels, and the first refactor of the `myfirst` blocking paths.
3. Timeouts and interruptible sleep, including signal handling and the choice between `EINTR`, `ERESTART`, and `EWOULDBLOCK`.
4. The `sx(9)` lock, which finally lets us express "many readers, occasional writer" without serializing every reader.
5. A multi-reader, single-writer scenario in the driver: a small configuration subsystem, the lock order between the data path and the configuration path, and the `WITNESS` discipline that keeps it correct.
6. Debugging synchronization issues, including a careful tour of `WITNESS`, the in-kernel debugger commands for inspecting locks, and the most common deadlock patterns.
7. Stress testing under realistic I/O patterns, with `lockstat(1)`, `dtrace(1)`, and the existing Chapter 11 testers extended for the new primitives.
8. Refactoring and versioning the driver: a clean `LOCKING.md`, a bumped version string, an updated changelog, and a regression run that validates the whole thing.

Hands-on labs and challenge exercises follow, then a troubleshooting reference, a wrapping-up section, and a bridge to Chapter 13.

If this is your first pass, read linearly and do the labs in order. If you are revisiting, the debugging section and the refactoring section stand alone and make good single-sitting reads.



## Section 1: What Is Synchronization in the Kernel?

Chapter 11 used the words *synchronization*, *locking*, *mutual exclusion*, and *coordination* somewhat interchangeably. That was acceptable when the only primitive on the table was the mutex, because the mutex collapses all those ideas into one mechanism. With the broader toolkit Chapter 12 introduces, the words start to mean different things, and getting them straight early prevents a lot of confusion later.

This section establishes the vocabulary. It also draws the small decision tree we will refer back to throughout the rest of the chapter when we ask "which primitive should I reach for here?".

### What Synchronization Means

**Synchronization** is the broader idea: any mechanism by which two or more concurrent threads of execution coordinate their access to shared state, their progress through a shared procedure, or their timing relative to each other.

Three flavours of coordination cover almost everything a driver needs:

**Mutual exclusion**: at most one thread at a time inside a critical region. Mutexes and exclusive locks deliver this. The guarantee is structural: while you are inside, nobody else is.

**Shared access with restricted writes**: many threads may inspect a value at the same time, but a thread that wants to change it must wait until everyone is out and nobody else is in. Shared/exclusive locks deliver this. The guarantee is asymmetric: readers tolerate each other; a writer tolerates nobody.

**Coordinated waiting**: a thread suspends until some condition becomes true, and another thread that knows the condition has become true wakes the waiter. Condition variables and the older `mtx_sleep` / `wakeup` channel mechanism deliver this. The guarantee is temporal: the waiter does not consume CPU while it is waiting; the waker does not have to know who is waiting; the kernel handles the rendezvous.

A driver typically uses all three. The cbuf in `myfirst` already uses two: mutual exclusion to protect the cbuf state, and coordinated waiting to suspend a reader when the buffer is empty. Chapter 12 adds the third (shared access) and refines the second (named condition variables instead of anonymous channels).

### Synchronization Versus Locking

It is tempting to think that *synchronization* and *locking* are the same word. They are not.

**Locking** is one technique for synchronization. It is the family of mechanisms that operate on a shared object and grant or refuse access to it. Mutexes, sx locks, rw locks, and lockmgr locks are all locks.

**Synchronization** includes locking but also includes coordinated waiting (condition variables, sleep channels), event signalling (semaphores), and timed coordination (callouts, timed sleeps). A waiting thread may not be holding any lock at the moment it is suspended (in fact, with `mtx_sleep` and `cv_wait`, the lock is dropped during the wait), and yet it is participating in synchronization with the threads that will eventually wake it.

The mental model that follows from this distinction is useful: locking is *about access*; coordinated waiting is *about progress*. Most non-trivial driver code mixes both. The mutex around the cbuf is locking. The sleep on `&sc->cb` while the buffer is empty is coordinated waiting. Both are synchronization. Either one alone would not be enough.

### Blocking Versus Spinning

Two basic shapes recur across the FreeBSD primitives. Knowing which shape a primitive uses is half the battle of choosing among them.

**Blocking primitives** put a contending thread to sleep on the kernel's sleep queue. The sleeping thread consumes no CPU; it will be made runnable again when the holding thread releases or the awaited condition is signalled. Blocking primitives are appropriate when the wait could be long, when the thread is in a context where sleeping is legal, and when keeping the CPU busy in a tight retry loop would harm overall throughput. `MTX_DEF` mutexes, `cv_wait`, `sx_xlock`, `sx_slock`, and `mtx_sleep` are all blocking.

**Spinning primitives** keep the thread on-CPU and busy-wait on the lock state, retrying atomically until the holder releases. They are appropriate only when the critical section is very short, when the thread cannot legally sleep (for example, inside a hardware interrupt filter), or when the cost of a context switch would dwarf the wait. `MTX_SPIN` mutexes and `rw(9)` locks are spinning. The kernel itself uses spin locks for the very lowest layers of the scheduler and the interrupt machinery.

Chapter 12 stays mostly in the blocking world. Our driver runs in process context; it is permitted to sleep; the gain from spinning would be marginal. The one exception is when we mention `rw(9)` as a sibling of `sx(9)` for completeness; the deeper treatment of `rw(9)` belongs to chapters where a driver uses it for a real reason.

### A Small Map of FreeBSD Primitives

The FreeBSD synchronization toolkit is larger than people expect. For driver work, eight primitives carry essentially all of the load:

| Primitive | Header | Behaviour | Best for |
|---|---|---|---|
| `mtx(9)` (`MTX_DEF`) | `sys/mutex.h` | Sleep mutex; one owner at a time | Default lock for most softc state |
| `mtx(9)` (`MTX_SPIN`) | `sys/mutex.h` | Spin mutex; disables interrupts | Short critical sections in interrupt context |
| `cv(9)` | `sys/condvar.h` | Named wait channel; pairs with a mutex | Coordinated waits with multiple distinct conditions |
| `sx(9)` | `sys/sx.h` | Sleep-mode shared/exclusive lock | Read-mostly state in process context |
| `rw(9)` | `sys/rwlock.h` | Spin-mode reader/writer lock | Read-mostly state in interrupt or short critical sections |
| `rmlock(9)` | `sys/rmlock.h` | Read-mostly lock; cheap reads, expensive writes | Hot read paths with rare configuration changes |
| `sema(9)` | `sys/sema.h` | Counting semaphore | Resource accounting; rarely needed in drivers |
| `epoch(9)` | `sys/epoch.h` | Read-mostly synchronization with deferred reclamation | Hot read paths in network/storage drivers |

The ones we use in this chapter, in addition to the mutex Chapter 11 introduced, are `cv(9)` and `sx(9)`. `rw(9)` is mentioned for context. `rmlock(9)`, `sema(9)`, and `epoch(9)` are deferred to later chapters where the driver in question actually justifies them.

### Atomics in the Same Map

Strictly speaking, the `atomic(9)` primitives Chapter 11 covered are not part of the synchronization toolkit at all. They are *concurrent operations*: indivisible memory accesses that compose with the locks but do not themselves provide blocking, waiting, or signalling. They sit alongside the locks in the way a power tool sits alongside a hand tool: useful for a specific job, not a substitute for the rest of the toolkit.

We will reach for atomics in this chapter only when a single-word read-modify-write is the right shape for what we want to express. For everything else, the locks and the condition variables earn their keep.

### A First Decision Tree

When you face a piece of shared state and you are deciding how to protect it, work the questions in this order. The first question that yields a definitive answer ends the search.

1. **Is the state a single word that needs a single read-modify-write?** Use an atomic. (Examples: a generation counter, a flag word.)
2. **Does the state have a composite invariant that spans more than one field, and is the access in process context?** Use an `MTX_DEF` mutex. (Examples: the cbuf head/used pair, a queue head/tail pair.)
3. **Is the access in interrupt context, or must the critical section disable preemption?** Use an `MTX_SPIN` mutex.
4. **Is the state read frequently from many threads but written rarely?** Use `sx(9)` for sleepable callers (most driver code) or `rw(9)` for short critical sections that may run in interrupt context.
5. **Do you need to wait until a specific condition becomes true (not just acquire a lock)?** Use a condition variable (`cv(9)`) paired with the mutex that protects the condition. The older `mtx_sleep`/`wakeup` channel mechanism is the legacy alternative; new code should prefer `cv(9)`.
6. **Do you need to bound a wait by wall time?** Use the timed variant (`cv_timedwait_sig`, `mtx_sleep` with a non-zero `timo` argument, `msleep_sbt(9)`) and design the caller to handle `EWOULDBLOCK`.

The tree compresses into a short slogan: *atomic for a word, mutex for a structure, sx for read-mostly, cv for waiting, timed for bounded waiting, spin only when you must*.

### A Worked Decision: Where Each `myfirst` State Sits

Walk the tree against each piece of state in your current driver. The exercise is short and useful.

- The cbuf indices and backing memory: composite invariant, process context. Use an `MTX_DEF` mutex. (This is what Chapter 11 chose.)
- `sc->bytes_read`, `sc->bytes_written`: high-frequency counters, rarely read. Use `counter(9)` per-CPU counters. (This is what Chapter 11 migrated to.)
- `sc->open_count`, `sc->active_fhs`: low-frequency integers, fine under the same mutex as the cbuf. No reason to split them out.
- `sc->is_attached`: a flag, read often at handler entry, written once per attach/detach. The Chapter 11 design reads it without the mutex as an optimization, re-checks after every sleep, and writes it under the mutex.
- The "is the buffer empty?" condition that read waiters block on: a coordinated wait. Currently uses `mtx_sleep(&sc->cb, ...)`. Section 2 will replace this with a named condition variable.
- The "is there room in the buffer?" condition that write waiters block on: another coordinated wait, currently sharing the same channel. Section 2 will give it its own condition variable.
- A future configuration subsystem (added in Section 5): read frequently by every I/O call, written occasionally by a sysctl handler. Use `sx(9)`.

Notice how the tree did the work. We did not have to invent a custom design for any of these; we asked the questions and the right primitive fell out.

### Real-World Analogy: Doors, Hallways, and Whiteboards

A small analogy for readers who like them. Imagine a research lab.

The cbuf is a precision instrument that only one person at a time can operate. The lab installs a door with a single key on it. Anyone who wants to use the instrument must take the key. While they have the key, nobody else can enter. That is a mutex.

The lab has a status whiteboard that lists the current calibration of the instrument. Anyone may read the whiteboard at any time; they do not interfere with each other. Only the lab manager updates the whiteboard, and they only do so after waiting for everyone else to step away. That is a shared/exclusive lock.

The lab has a coffee pot. People who want coffee but find the pot empty leave a note on the bulletin board: "I am in the lounge; wake me when there is coffee." When somebody brews a fresh pot, they check the bulletin board and tap the shoulders of everyone whose note is for "coffee", regardless of how long ago they wrote it. That is a condition variable.

The same person who left the coffee note may also leave a second note: "but only wait fifteen minutes; if no coffee by then, I am going to the cafeteria." That is a timed wait.

Each mechanism in the lab matches a real coordination problem. None of them is a substitute for any of the others. The same is true in the kernel.

### Comparing Primitives Side by Side

Sometimes seeing the primitives next to each other in a single table makes the choice immediate. The properties that differ across them are: whether they block or spin, whether they support shared (multi-reader) access, whether they are sleepable from the perspective of the holder, whether they support priority propagation, whether they are interruptible by signals, and whether the calling context can include sleeping operations.

| Property | `mtx(9) MTX_DEF` | `mtx(9) MTX_SPIN` | `sx(9)` | `rw(9)` | `cv(9)` |
|---|---|---|---|---|---|
| Behaviour when contended | Sleep | Spin | Sleep | Spin | Sleep |
| Multiple holders | No | No | Yes (shared) | Yes (read) | n/a (waiters) |
| Caller may sleep while holding | Yes | No | Yes | No | n/a |
| Priority propagation | Yes | No (interrupts disabled) | No | Yes | n/a |
| Signal-interruptible variant | n/a | n/a | `_sig` | No | `_sig` |
| Has timed wait variant | `mtx_sleep` w/ timo | n/a | n/a | n/a | `cv_timedwait` |
| Suitable in interrupt context | No | Yes | No | Yes (with care) | No |

Two things stand out. First, the column for `cv(9)` does not really fit the same questions because cv is not a lock; it is a wait primitive. We include it in the comparison because the choice "should I wait or spin?" is essentially the same as "should I block on a cv or spin on an `MTX_SPIN`?". Second, the priority propagation column distinguishes `mtx(9)` and `rw(9)` from `sx(9)`. `sx(9)` does not propagate priority because its sleep queues do not support it. In practice this matters only for real-time workloads; ordinary drivers do not notice.

Use the table as a quick lookup when you face a new piece of state. The decision tree above gives you the *order* to ask the questions; the table gives you the *answer* once you have asked them.

### A Note on Semaphores

FreeBSD also has a counting semaphore primitive (`sema(9)`) that is occasionally useful. A semaphore is a counter; threads decrement it (via `sema_wait` or `sema_trywait`) and block if the counter is zero; threads increment it (via `sema_post`) and may wake a waiter. The classic use is bounded-resource accounting: a queue with a maximum length where producers block when the queue is full and consumers block when it is empty.

Most driver problems that look semaphore-shaped can be solved equally well with a mutex plus a condition variable. The cv approach has the advantage that you can attach naming to each condition; the semaphore is anonymous. The semaphore has the advantage that the waiting and signalling are part of the primitive itself, with no need for a separate interlock.

This chapter does not use `sema(9)`. We mention it for completeness; if you encounter it in real driver source, you now know what shape it has.

### Wrapping Up Section 1

Synchronization is broader than locking, locking is broader than mutual exclusion, and the FreeBSD toolkit gives you a different primitive for each shape of coordination problem you might encounter. Atomics for single-word updates, mutexes for composite invariants, sx locks for read-mostly state, condition variables for coordinated waiting, timed sleeps for bounded waiting, and spin variants only when the calling context demands them.

That decision tree will guide every choice we make for the rest of the chapter. Section 2 starts with the first refactor: turning the anonymous `&sc->cb` wakeup channel from Chapter 11 into a pair of named condition variables.



## Section 2: Condition Variables and Sleep/Wakeup

The `myfirst` driver as Chapter 11 left it has two distinct conditions that block the I/O paths. A reader sleeps when `cbuf_used(&sc->cb) == 0` and waits for "data has arrived". A writer sleeps when `cbuf_free(&sc->cb) == 0` and waits for "room has appeared". Both currently sleep on the same anonymous channel, `&sc->cb`. Both wakeups call `wakeup(&sc->cb)` after every state change, which wakes every sleeper regardless of which condition triggered the change.

That arrangement works. It is also wasteful, opaque, and harder to reason about than it needs to be. This section introduces condition variables (`cv(9)`), the cleaner FreeBSD primitive for the same coordinated-wait pattern, and walks through the refactor that gives each condition its own variable.

### Why Mutex Plus Wakeup Is Not Enough

Chapter 11 used `mtx_sleep(chan, mtx, pri, wmesg, timo)` and `wakeup(chan)` to coordinate the read and write paths. The pair has the great virtue of simplicity: any pointer can be a channel, the kernel keeps a hash table of waiters per channel, and a `wakeup` on the right channel finds them all.

The vices appear as the driver grows.

**The channel is anonymous.** A reader of the source sees `mtx_sleep(&sc->cb, &sc->mtx, PCATCH, "myfrd", 0)` and has to infer, from the context and the wmesg string, what condition the thread is waiting for. There is nothing in `&sc->cb` that says "data available" instead of "room available" or "device detached". The channel is just a pointer the kernel uses as a hash key; the meaning lives in the convention.

**Multiple conditions share one channel.** When `myfirst_write` finishes a write, it calls `wakeup(&sc->cb)`. That wakes every waiter on `&sc->cb`, including readers waiting for data (correct) and writers waiting for room (incorrect; a write does not free room, it consumes it). Each unwanted waiter goes back to sleep after rechecking its condition. This is the *thundering herd* problem in miniature: the wakeup is correct but expensive.

**Lost wakeups are still possible if you are careless.** If you ever drop the mutex between checking the condition and entering `mtx_sleep`, a wakeup can fire in that window and be missed. Chapter 11 explained that `mtx_sleep` itself is atomic with the lock drop, which closes the window; but the rule is implicit in the API and easy to violate when refactoring.

**The wmesg argument is the only label.** A debugging engineer who runs `procstat -kk` against a hung process sees `myfrd` and has to remember what that means. The string is at most seven characters; it is a hint, not a structured description.

Condition variables solve all four of these. Each cv has a name (its description string is checkable from `dtrace` and `procstat`). Each cv represents exactly one logical condition; there is no question of which waiters a `cv_signal` will affect. The `cv_wait` primitive enforces the atomicity of the lock drop by taking the mutex as an argument, so misuse is much harder. And the relationship between the waiter and the waker is expressed in the type itself: both sides reference the same `struct cv`.

### What a Condition Variable Is

A **condition variable** is a kernel object that represents a logical condition some threads are waiting for and others will eventually signal. The condition variable does not store the condition; the condition lives in your driver's state, protected by your mutex. The condition variable is the rendezvous point: the place where waiters queue up and where wakers find them.

The data structure is small and lives in `/usr/src/sys/sys/condvar.h`:

```c
struct cv {
        const char      *cv_description;
        int              cv_waiters;
};
```

Two fields: a description string used for debugging, and a count of currently-waiting threads (an optimization to skip the wakeup machinery when no one is waiting).

The API is also small:

```c
void  cv_init(struct cv *cvp, const char *desc);
void  cv_destroy(struct cv *cvp);

void  cv_wait(struct cv *cvp, struct mtx *mtx);
int   cv_wait_sig(struct cv *cvp, struct mtx *mtx);
void  cv_wait_unlock(struct cv *cvp, struct mtx *mtx);
int   cv_timedwait(struct cv *cvp, struct mtx *mtx, int timo);
int   cv_timedwait_sig(struct cv *cvp, struct mtx *mtx, int timo);

void  cv_signal(struct cv *cvp);
void  cv_broadcast(struct cv *cvp);
void  cv_broadcastpri(struct cv *cvp, int pri);

const char *cv_wmesg(struct cv *cvp);
```

(The actual prototypes use `struct lock_object` rather than `struct mtx`; the macros in `condvar.h` substitute the right `&mtx->lock_object` pointer for you. The form above is the form you will write in driver code.)

A few rules and conventions matter from the start.

`cv_init` is called once, after the cv structure exists in memory and before any waiter or waker can reach it. The matching `cv_destroy` is called once, after every waiter has either woken up or been forced off the queue, and before the cv structure is freed. Lifetime mistakes here cause the same kind of catastrophic crashes that mutex lifetime mistakes cause.

`cv_wait` and its variants must be called with the interlock mutex *held*. Inside `cv_wait`, the kernel atomically drops the mutex and puts the calling thread on the cv's wait queue. When the thread is awakened, the mutex is reacquired before `cv_wait` returns. From your code's perspective, the mutex is held both before and after the call; a different thread could not have observed the gap, even though the gap really existed. This is exactly the same atomic-drop-and-sleep contract `mtx_sleep` provides.

`cv_signal` wakes one waiter, and `cv_broadcast` wakes all waiters. Which waiter `cv_signal` picks is worth stating carefully. The `condvar(9)` man page only promises that it unblocks "one waiter"; it does *not* promise strict FIFO order, and your code must not rely on a specific ordering. What the current FreeBSD 14.3 implementation actually does, inside `sleepq_signal(9)` in `/usr/src/sys/kern/subr_sleepqueue.c`, is scan the cv's sleep queue and pick the thread with the highest priority, breaking ties in favour of the thread that has been sleeping the longest. That is a useful mental model, but treat it as an implementation detail rather than an API guarantee. If correctness depends on which thread wakes up next, your design is probably wrong and should use a different primitive or an explicit queue. Both `cv_signal` and `cv_broadcast` are typically called with the interlock mutex held, although the rule is more about correctness of the surrounding logic than about the primitive itself: if you call `cv_signal` without the interlock, it is possible for a new waiter to arrive and miss the signal. The standard discipline is therefore "hold the mutex, change the state, signal, drop the mutex".

`cv_wait_sig` returns nonzero if the thread was awakened by a signal (typically `EINTR` or `ERESTART`); zero if it was awakened by `cv_signal` or `cv_broadcast`. Drivers that want their blocking I/O paths to honour Ctrl-C use `cv_wait_sig`, not `cv_wait`. Section 3 explores the signal-handling rules in depth.

`cv_wait_unlock` is the rare variant for when the caller wants the interlock dropped on the *waiting* side and not reacquired on return. Useful in teardown sequences where the caller has no further business with the interlock once the wait completes. Drivers seldom need it; we mention it because you will see it in a few places in the FreeBSD tree, and the chapter does not use it further.

`cv_timedwait` and `cv_timedwait_sig` add a timeout in ticks. They return `EWOULDBLOCK` if the timeout fires before any wakeup arrives. Section 3 explains how to bound a blocking operation with these.

### A Worked Refactor: Adding Two Condition Variables to myfirst

The Chapter 11 driver had one anonymous channel for both conditions. Stage 1 of this chapter splits it into two named condition variables: `data_cv` ("data is available to read") and `room_cv` ("room is available to write").

Add two fields to the softc:

```c
struct myfirst_softc {
        /* ... existing fields ... */
        struct cv               data_cv;
        struct cv               room_cv;
        /* ... existing fields ... */
};
```

Initialize and destroy them in attach and detach:

```c
static int
myfirst_attach(device_t dev)
{
        /* ... existing setup ... */
        cv_init(&sc->data_cv, "myfirst data");
        cv_init(&sc->room_cv, "myfirst room");
        /* ... rest of attach ... */
}

static int
myfirst_detach(device_t dev)
{
        /* ... existing teardown that cleared is_attached and woke sleepers ... */
        cv_destroy(&sc->data_cv);
        cv_destroy(&sc->room_cv);
        /* ... rest of detach ... */
}
```

A small but important subtlety: detach must not destroy a cv that still has waiters. The Chapter 11 detach path already wakes sleepers and refuses to proceed while `active_fhs > 0`, which means by the time we reach `cv_destroy`, no descriptor is open and no thread can still be inside `cv_wait`. We add a `cv_broadcast(&sc->data_cv)` and `cv_broadcast(&sc->room_cv)` immediately before destroying, as belt-and-braces, in case any background path ever does sneak in.

Update the wait helpers to use the new variables:

```c
static int
myfirst_wait_data(struct myfirst_softc *sc, int ioflag, ssize_t nbefore,
    struct uio *uio)
{
        int error;

        MYFIRST_ASSERT(sc);
        while (cbuf_used(&sc->cb) == 0) {
                if (uio->uio_resid != nbefore)
                        return (-1);
                if (ioflag & IO_NDELAY)
                        return (EAGAIN);
                error = cv_wait_sig(&sc->data_cv, &sc->mtx);
                if (error != 0)
                        return (error);
                if (!sc->is_attached)
                        return (ENXIO);
        }
        return (0);
}

static int
myfirst_wait_room(struct myfirst_softc *sc, int ioflag, ssize_t nbefore,
    struct uio *uio)
{
        int error;

        MYFIRST_ASSERT(sc);
        while (cbuf_free(&sc->cb) == 0) {
                if (uio->uio_resid != nbefore)
                        return (-1);
                if (ioflag & IO_NDELAY)
                        return (EAGAIN);
                error = cv_wait_sig(&sc->room_cv, &sc->mtx);
                if (error != 0)
                        return (error);
                if (!sc->is_attached)
                        return (ENXIO);
        }
        return (0);
}
```

Three things changed and nothing else. `mtx_sleep(&sc->cb, &sc->mtx, PCATCH, "myfrd", 0)` became `cv_wait_sig(&sc->data_cv, &sc->mtx)`. The corresponding line in the write path became `cv_wait_sig(&sc->room_cv, &sc->mtx)`. The wmesg string is gone (the cv's description string takes its place), the channel is now a real object with a name, and the `PCATCH` flag is implicit in the `_sig` suffix.

Update the wakers. After a successful read, instead of waking everyone on `&sc->cb`, wake only the writers waiting for room:

```c
got = myfirst_buf_read(sc, bounce, take);
fh->reads += got;
MYFIRST_UNLOCK(sc);

if (got > 0) {
        cv_signal(&sc->room_cv);
        selwakeup(&sc->wsel);
}
```

After a successful write, wake only the readers waiting for data:

```c
put = myfirst_buf_write(sc, bounce, want);
fh->writes += put;
MYFIRST_UNLOCK(sc);

if (put > 0) {
        cv_signal(&sc->data_cv);
        selwakeup(&sc->rsel);
}
```

Two improvements come together. First, a successful read no longer wakes other readers (a wasted wakeup that immediately goes back to sleep); only writers, who actually have a use for the freed room, are woken. Symmetric on the write side. Second, the source is now self-explanatory: `cv_signal(&sc->room_cv)` reads as "there is room now"; the reader does not have to remember what `&sc->cb` means.

Notice we added a `if (got > 0)` and `if (put > 0)` guard before the signal. There is no point waking a sleeper if nothing changed; the empty signal is benign but cheap to skip. This is a small optimization and a clarification: the signal is announcing a state change, and the guard says so.

`cv_signal` instead of `cv_broadcast`: we wake one waiter per state change, not all of them. The state change (one byte freed by a read, one byte added by a write) is enough for one waiter to make progress. If multiple waiters are blocked, the next signal will wake the next one. This is the per-event correspondence the cv API encourages.

### When to Signal Versus Broadcast

`cv_signal` wakes one waiter. `cv_broadcast` wakes all of them. The choice matters more than people expect.

Use `cv_signal` when:

- The state change is a per-event update (one byte arrived; one descriptor freed; one packet enqueued). One waiter making progress is enough; the next event will wake the next waiter.
- All waiters are equivalent and any one of them can consume the change.
- The cost of waking a waiter that has nothing to do is non-trivial (because the waiter immediately re-checks the condition and sleeps again).

Use `cv_broadcast` when:

- The state change is global and all waiters need to know (the device is being detached; the configuration changed; the buffer was reset).
- The waiters are not equivalent; each may be waiting on a slightly different sub-condition that the broadcast resolves.
- You want to avoid the bookkeeping of figuring out which subset of waiters can proceed, at the cost of waking some that will go back to sleep.

For the `myfirst` data and room conditions, `cv_signal` is the right call. For the detach path, `cv_broadcast` is the right call: the detach must wake every blocked thread so they can return `ENXIO` and exit cleanly.

Add the broadcasts to detach:

```c
MYFIRST_LOCK(sc);
sc->is_attached = 0;
cv_broadcast(&sc->data_cv);
cv_broadcast(&sc->room_cv);
MYFIRST_UNLOCK(sc);
```

That replaces the Chapter 11 `wakeup(&sc->cb)`. The two broadcasts wake every reader and every writer that may be sleeping; each one re-checks `is_attached`, sees that it is now zero, and returns `ENXIO`.

### A Subtle Trap: cv_signal Without the Mutex

The standard discipline says "hold the mutex, change the state, signal, drop the mutex". You may have noticed our refactor signals *after* dropping the mutex (the `MYFIRST_UNLOCK(sc)` precedes the `cv_signal`). Is that wrong?

It is not wrong, and the reason is worth understanding.

The race the discipline is meant to prevent is this: a waiter checks the condition (false), is about to call `cv_wait`, but has dropped the mutex. The waker now changes the state, sees no one in the cv's wait queue (because the waiter has not yet enqueued), and skips the signal. The waiter then enqueues and sleeps forever.

`cv_wait` itself prevents that race by enqueueing the waiter on the cv *before* dropping the mutex. The kernel's internal cv-queue lock is acquired while the caller's mutex is still held, the thread is added to the wait queue, and only then is the caller's mutex dropped and the thread descheduled. Any subsequent `cv_signal` on that cv, with or without the caller's mutex held, will find the waiter and wake it.

The discipline of signalling under the mutex is therefore a defensive convention rather than a strict requirement. We follow it for the simple cases (because it is harder to get wrong) and we relax it for the case where signalling outside the mutex is a measurable improvement (it lets the woken thread acquire the mutex without contending with the signaller). For `myfirst`, signalling after `MYFIRST_UNLOCK(sc)` shaves a few cycles off the wake path; for safety we still take care to not allow a window between the state change and the signal where the state could be reverted. In our refactor, the only thread that can revert the state is also operating under the mutex, so the window is closed.

If you are uncertain, signal under the mutex. It is the safer default and the cost is negligible.

### Verifying the Refactor

Build the new driver and load it on a `WITNESS` kernel. Run the Chapter 11 stress kit. Three things should happen:

- All tests pass with the same byte-count semantics as before.
- `dmesg` is silent. No new warnings.
- `procstat -kk` against a sleeping reader now shows the cv's description in the wait-channel column. Tools that report `wmesg` truncate to `WMESGLEN` (eight characters, defined in `/usr/src/sys/sys/user.h`); a description of `"myfirst data"` therefore shows up as `"myfirst "` in `procstat` and `ps`. The full description string remains visible to `dtrace` (which reads `cv_description` directly) and in the source. If you want the truncated form to be more informative, choose shorter descriptions such as `"mfdata"` and `"mfroom"`; the chapter keeps the longer, more readable names because dtrace and the source code use the full string and that is where you spend most of your debugging time.

`lockstat(1)` will show fewer cv events than the old `wakeup` mechanism produced wakeups, because the per-condition signalling does not wake threads that have nothing to do. This is the throughput improvement we expected.

### A Mental Model: How a cv_wait Plays Out

For readers who learn best from a step-by-step picture, here is the sequence of events when a thread calls `cv_wait_sig` and is later signalled.

Time t=0: thread A is in `myfirst_read`. The cbuf is empty.

Time t=1: thread A calls `MYFIRST_LOCK(sc)`. The mutex is acquired. Thread A is now the only thread in any critical section protected by `sc->mtx`.

Time t=2: thread A enters the wait helper. The check `cbuf_used(&sc->cb) == 0` is true. Thread A calls `cv_wait_sig(&sc->data_cv, &sc->mtx)`.

Time t=3: inside `cv_wait_sig`, the kernel takes the cv-queue spin lock for `data_cv`, increments `data_cv.cv_waiters`, and atomically does two things: drops `sc->mtx` and adds thread A to the cv's wait queue. Thread A's state changes to "sleeping on data_cv".

Time t=4: thread A is descheduled. The CPU runs other threads.

Time t=5: thread B enters `myfirst_write` from another process. Thread B calls `MYFIRST_LOCK(sc)`. The mutex is currently free; thread B acquires it.

Time t=6: thread B reads from user space (`uiomove`), commits bytes to the cbuf, updates counters. Thread B calls `MYFIRST_UNLOCK(sc)`.

Time t=7: thread B calls `cv_signal(&sc->data_cv)`. The kernel takes the cv-queue spin lock, finds thread A on the wait queue, decrements `cv_waiters`, removes thread A from the queue, and marks thread A as runnable.

Time t=8: the scheduler decides thread A is the highest-priority runnable thread (or one of several; FIFO among equal). Thread A is scheduled onto a CPU.

Time t=9: thread A resumes inside `cv_wait_sig`. The function reacquires `sc->mtx` (this may itself block if another thread now holds the mutex; if so, thread A is added to the mutex's wait list). Thread A returns from `cv_wait_sig` with return value 0 (normal wakeup).

Time t=10: thread A continues in the wait helper. The `while (cbuf_used(&sc->cb) == 0)` check is now false (thread B added bytes). The loop exits.

Time t=11: thread A reads from the cbuf and proceeds.

Three things to take away from the picture. First, the lock state is consistent at every step. The mutex is either held by exactly one thread or held by none; thread A's view of the world is the same before the wait as after. Second, the wakeup is decoupled from the actual scheduling; thread B did not hand the CPU directly to thread A. Third, there is a window between t=9 and t=10 where thread A holds the mutex and another writer could (if it had been waiting) potentially fill the buffer further. That is fine; thread A's check is on the cbuf state at t=10, not at t=7.

This sequence is the canonical "wait, signal, wake, re-check, proceed" pattern. Every cv use in the chapter is an instance of it.

### A Look at kern_condvar.c

If you have ten minutes, open `/usr/src/sys/kern/kern_condvar.c` and skim. Three functions are particularly worth seeing:

`cv_init` (top of file): very short. It just initializes the description and zeroes the waiter count.

`_cv_wait` (mid-file): the core blocking primitive. It takes the cv-queue spin lock, increments `cv_waiters`, drops the caller's interlock, calls the sleep queue machinery to enqueue the thread and yield, and on return decrements `cv_waiters` and reacquires the interlock. The atomic drop-and-sleep is performed by the sleep queue layer, exactly the same machinery that backs `mtx_sleep`. There is nothing magical about cv on top of sleep queues; it is a thin, named interface.

`cv_signal` and `cv_broadcastpri`: each takes the cv-queue spin lock, finds one (or all) waiters, and uses `sleepq_signal` or `sleepq_broadcast` to wake them.

The takeaway: condition variables are a thin, structured layer on top of the same sleep queue primitives `mtx_sleep` uses. They are not slower; they are not faster; they are clearer.

### Wrapping Up Section 2

The refactor in this section gives each waiting condition its own object, its own name, its own waiters' queue, and its own signal. The driver behaves the same to user space, but it now reads more honestly: `cv_signal(&sc->room_cv)` says "there is room", which is what we mean. The `WITNESS` discipline is preserved; the `mtx_assert` calls in the helpers still hold; the test kit continues to pass. We have moved one level up the synchronization vocabulary without losing any of the safety Chapter 11 built.

Section 3 turns to the orthogonal question of *how long should we wait?*. Indefinite blocking is convenient for the implementation but harsh on the user. Timed waits and signal-aware waits are how a well-behaved driver responds to the world.



## Section 3: Handling Timeouts and Interruptible Sleep

A blocking primitive is, by default, indefinite. A reader that calls `cv_wait_sig` when the buffer is empty will sleep until either someone calls `cv_signal` (or `cv_broadcast`) on the same cv, or a signal is delivered to the reader's process. From the kernel's point of view, "indefinite" is a perfectly respectable answer. From the user's point of view, "indefinite" is a hang.

This section is about the two ways the FreeBSD synchronization primitives let you bound a wait: by a wall-clock timeout and by an interruption from a signal. Both are simple to use and both have surprisingly subtle rules around return values. We start with the easier one and work up.

### What Goes Wrong With Indefinite Sleeps

Three real problems push us to use timed and interruptible sleeps in a driver.

**Hung programs.** A user runs `cat /dev/myfirst` in a terminal. There is no producer. The `cat` blocks in `read(2)`, which blocks in `myfirst_read`, which blocks in `cv_wait_sig`. The user presses Ctrl-C. If the wait is interruptible (the `_sig` variant), the kernel delivers `EINTR` and the user gets their shell back. If it is not (`cv_wait` without `_sig`), the kernel ignores the signal and the user has to use Ctrl-Z and `kill %1` from another terminal. Most users do not know how to do that. They reach for the reset button.

**Stuck progress.** A device driver waits for an interrupt that never arrives because the hardware is wedged. The driver's I/O thread sleeps forever. The whole system slowly fills with processes blocked on this driver. Eventually an administrator notices, but by then there is nothing to do but reboot. A bounded wait would have caught this much earlier.

**Bad user experience.** A network protocol expects a response within a specified time. A storage operation expects a completion within a service-level agreement. Neither is well-served by a primitive that can wait forever. The driver should be able to enforce a deadline and return a clean error when the deadline is missed.

The FreeBSD primitives that solve these are `cv_wait_sig` and `cv_timedwait_sig`, with the older `mtx_sleep` and `tsleep` family providing the same capabilities through a different shape. We have already met `cv_wait_sig` in Section 2. Here we look more carefully at what its return value tells us and how to add an explicit timeout.

### The Three Wait Outcomes

Any blocking sleep primitive can return for one of three reasons:

1. **A normal wakeup.** Some other thread called `cv_signal`, `cv_broadcast`, or (in the legacy API) `wakeup`. The condition this thread was waiting for has changed, and the thread should re-check it.
2. **A signal was delivered to the process.** The thread is being asked to abandon the wait so the signal handler can run. The driver typically returns `EINTR` to user space, which is also the sleep primitive's return value.
3. **A timeout fired.** The thread was waiting with a deadline and the deadline expired before any wakeup arrived. The sleep primitive returns `EWOULDBLOCK`.

The driver's job is to figure out which of the three happened and respond appropriately.

The first case is the easy one. The thread re-checks its condition (the `while` loop around `cv_wait_sig` is what does this); if the condition is now true, the loop ends and the I/O proceeds; if not, the thread sleeps again.

The second case is more interesting. The kernel delivers a signal not to a *thread* but to a *process*. A signal can be a serious condition (`SIGTERM`, `SIGKILL`) or a routine one (`SIGINT` from Ctrl-C, `SIGALRM` from a timer). The thread that was sleeping needs to return to user space promptly so the signal handler can run. The convention is that the sleep primitive returns `EINTR` (interrupted system call), the driver returns `EINTR` from its handler, and the kernel either restarts the system call (if the handler returned with `SA_RESTART`) or returns `EINTR` to user space (if not).

The third case is the bounded-wait case. The driver typically maps `EWOULDBLOCK` to either `EAGAIN` (try again later) or to a more specific error (`ETIMEDOUT`, where appropriate).

### EINTR, ERESTART, and the Restart Question

A subtlety lurks in case 2 that is worth understanding before the chapter goes any further.

When `cv_wait_sig` is interrupted by a signal, the actual return value is one of two things:

- `EINTR` if the signal's disposition is "do not restart system calls". The kernel returns `EINTR` to user space, and `read(2)` reports `-1` with `errno == EINTR`. The user program is responsible for retrying if it wants to.
- `ERESTART` if the signal's disposition is "restart system calls" (the `SA_RESTART` flag). The kernel transparently re-enters the syscall and the wait happens again. The user program does not see the interruption.

A driver should not return `ERESTART` directly to user space; it is an internal sentinel for the syscall layer. If the driver returns `ERESTART` from its handler, the syscall layer knows to restart. If the driver returns `EINTR`, the syscall layer returns `EINTR` to user space.

The convention most drivers follow: pass the return value of `cv_wait_sig` through unchanged. If you got `EINTR`, the driver returns `EINTR`. If you got `ERESTART`, the driver returns `ERESTART`. The kernel takes it from there. The Chapter 11 driver did this implicitly; the Chapter 12 refactor in Section 2 continues to do so:

```c
error = cv_wait_sig(&sc->data_cv, &sc->mtx);
if (error != 0)
        return (error);
```

Returning `error` directly is the right move. Chapter 12 changes nothing about this rule; it just makes the rule visible in the new APIs.

### Adding a Timeout to the Read Path

Now the bounded-wait case. Suppose we want `myfirst_read` to optionally wait for at most some configurable duration before returning `EAGAIN` if no data arrives. (We use `EAGAIN` rather than `ETIMEDOUT` because `EAGAIN` is the conventional UNIX answer to "the operation would block; try again later".)

The driver needs three things:

1. A configuration value for the timeout (in milliseconds, say). Zero means "block indefinitely as before".
2. A way to convert the timeout to ticks, since `cv_timedwait_sig` takes its `timo` argument in ticks.
3. A loop that handles the three outcomes correctly: normal wakeup, signal interruption, timeout.

Add the configuration field to the softc:

```c
int     read_timeout_ms;  /* 0 = no timeout */
```

Initialize it in attach:

```c
sc->read_timeout_ms = 0;
```

Expose it as a sysctl:

```c
SYSCTL_ADD_INT(&sc->sysctl_ctx, SYSCTL_CHILDREN(sc->sysctl_tree),
    OID_AUTO, "read_timeout_ms", CTLFLAG_RW,
    &sc->read_timeout_ms, 0,
    "Read timeout in milliseconds (0 = block indefinitely)");
```

We use a plain `SYSCTL_ADD_INT` for now; the value is one integer, the read is atomic at the word level on amd64, and a slightly stale value is acceptable. (Section 5 will give us a more disciplined way to handle configuration changes.)

Update the wait helper:

```c
static int
myfirst_wait_data(struct myfirst_softc *sc, int ioflag, ssize_t nbefore,
    struct uio *uio)
{
        int error, timo;

        MYFIRST_ASSERT(sc);
        while (cbuf_used(&sc->cb) == 0) {
                if (uio->uio_resid != nbefore)
                        return (-1);
                if (ioflag & IO_NDELAY)
                        return (EAGAIN);

                timo = sc->read_timeout_ms;
                if (timo > 0) {
                        int ticks_total = (timo * hz + 999) / 1000;
                        error = cv_timedwait_sig(&sc->data_cv, &sc->mtx,
                            ticks_total);
                } else {
                        error = cv_wait_sig(&sc->data_cv, &sc->mtx);
                }
                if (error == EWOULDBLOCK)
                        return (EAGAIN);
                if (error != 0)
                        return (error);
                if (!sc->is_attached)
                        return (ENXIO);
        }
        return (0);
}
```

A few details deserve commentary.

The `(timo * hz + 999) / 1000` arithmetic converts milliseconds to ticks, rounding up. We want at least the requested wait, never less. A timeout of 1 ms on a 1000 Hz kernel becomes 1 tick. A timeout of 1 ms on a 100 Hz kernel becomes 1 tick (rounded up from 0.1). A timeout of 5500 ms becomes 5500 ticks at 1000 Hz, or 550 at 100 Hz.

The branch on `timo > 0` picks `cv_timedwait_sig` when a positive timeout is requested and `cv_wait_sig` (no timeout) when not. We could always call `cv_timedwait_sig` with `timo = 0`, but the cv API treats `timo = 0` as "wait indefinitely", and the behaviour is identical to `cv_wait_sig`. The explicit branch makes the intent clearer for a reader.

The `EWOULDBLOCK -> EAGAIN` translation gives user space the conventional "try again" indication. A user program that gets `EAGAIN` knows what to do; a user program that gets `ETIMEDOUT` would have to learn a new error code.

The `is_attached` re-check after every sleep remains. Even with a bounded wait, the device might have been detached during the sleep; the cv broadcast in detach (added in Section 2) wakes us; the timeout itself does not skip the post-sleep checks.

Apply the symmetric change to `myfirst_wait_room` if you want bounded writes, with a separate `write_timeout_ms` sysctl. The companion source does both.

### Verifying the Timeout

A small user-space tester confirms the new behaviour. Set the timeout to 100 ms, open the device with no producer, and read. You should see `read(2)` return `-1` with `errno == EAGAIN` after approximately 100 ms, not block forever.

```c
/* timeout_tester.c: confirm bounded reads. */
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <unistd.h>

#define DEVPATH "/dev/myfirst"

int
main(void)
{
        int timeout_ms = 100;
        size_t sz = sizeof(timeout_ms);

        if (sysctlbyname("dev.myfirst.0.read_timeout_ms",
            NULL, NULL, &timeout_ms, sz) != 0)
                err(1, "sysctlbyname set");

        int fd = open(DEVPATH, O_RDONLY);
        if (fd < 0)
                err(1, "open");

        char buf[1024];
        struct timeval t0, t1;
        gettimeofday(&t0, NULL);
        ssize_t n = read(fd, buf, sizeof(buf));
        gettimeofday(&t1, NULL);
        int saved = errno;

        long elapsed_ms = (t1.tv_sec - t0.tv_sec) * 1000 +
            (t1.tv_usec - t0.tv_usec) / 1000;
        printf("read returned %zd, errno=%d (%s) after %ld ms\n",
            n, saved, strerror(saved), elapsed_ms);

        close(fd);
        return (0);
}
```

Run with no producer attached. Expect output similar to:

```text
read returned -1, errno=35 (Resource temporarily unavailable) after 102 ms
```

Errno 35 on FreeBSD is `EAGAIN`. The 102 ms is the 100 ms timeout plus a couple of milliseconds of scheduling jitter.

Reset the sysctl to zero (`sysctl -w dev.myfirst.0.read_timeout_ms=0`) and rerun. Now `read(2)` blocks until you press Ctrl-C, at which point it returns `-1` with `errno == EINTR`. The interruptibility (the `_sig` suffix) and the timeout (the `_timedwait` variant) are independent capabilities. We can have neither, either, or both, and the API exposes each on its own switch.

### Choosing Between EAGAIN and ETIMEDOUT

When the timeout fires, the driver chooses what error to report. The two reasonable choices are `EAGAIN` and `ETIMEDOUT`.

`EAGAIN` (errno value 35 on FreeBSD; symbolic value `EWOULDBLOCK` is `#define`d to the same number in `/usr/src/sys/sys/errno.h`) is the conventional UNIX answer to "the operation would block". User programs that handle `O_NONBLOCK` understand it. Many user programs already retry on `EAGAIN`. Returning `EAGAIN` for a timeout is a safe default; it does the right thing for the majority of callers.

`ETIMEDOUT` (errno value 60 on FreeBSD) is more specific: "the operation has a deadline and the deadline expired". Network protocols use it; it means something different from "would block right now". A user program that wants to distinguish "no data yet, retry" from "no data after the agreed deadline, give up" needs `ETIMEDOUT`.

For `myfirst`, we use `EAGAIN`. The driver does not have a deadline contract with the caller; the timeout is a politeness, not a guarantee. Other drivers may make the other choice; both are legal.

### A Note on Fairness Under Timeouts

The timed wait does not change the cv's fairness story. `cv_timedwait_sig` is implemented in terms of the same sleep queue used by `cv_wait_sig`. When a wakeup arrives, the sleep queue picks the highest-priority waiter (FIFO among equal priorities) regardless of whether each waiter has a timeout pending. The timeout is a per-waiter watchdog; it does not affect the order in which non-timed-out waiters are woken.

Practical consequence: a thread with a 50 ms timeout and a thread without a timeout, both waiting on the same cv, will both be woken by `cv_signal` in the order the sleep queue chooses. The 50 ms thread does not get priority. If you need the timed waiter to be woken first, you have a different design problem (priority queues, separate cvs per priority class) that goes beyond this chapter.

For `myfirst`, all readers are equivalent and the lack of timeout-based prioritization is fine.

### When to Use a Timeout

Timeouts are not free. Each timed wait sets up a callout that fires the cv's wakeup if no real wakeup arrives first. The callout has a small per-tick cost and contributes to the kernel's overall callout pressure. Drivers that use timeouts for every blocking call create more callout traffic than drivers that block indefinitely.

Three rules of thumb:

- Use a timeout when the caller has a real deadline (a network protocol, a hardware watchdog, a user-visible response).
- Use a timeout when the wait has a "this should not be possible" backstop. A driver that "just in case" sets a 60 second timeout on a wait that should normally complete in microseconds is using the timeout as a sanity check, not as a deadline. That is fine.
- Do not use a timeout when the wait is naturally indefinite (a `cat /dev/myfirst` on an idle device should block until data arrives or the user gives up; either is fine, neither needs a timeout).

The `myfirst` driver in this chapter exposes a per-device sysctl that lets the user choose. The default of zero (block indefinitely) is the right default for a pseudo-device. Real drivers may have stronger opinions.

### Sub-Tick Precision With sbintime_t

The `cv_timedwait` and `cv_timedwait_sig` macros take their timeout in ticks. A tick on FreeBSD 14.3 is typically one millisecond (because `hz=1000` is the default), so tick precision is millisecond precision. For most driver use cases this is enough. Network and storage drivers occasionally want microsecond precision, and the `_sbt` (scaled binary time) variants are how you get it.

The relevant primitives:

```c
int  cv_timedwait_sbt(struct cv *cvp, struct mtx *mtx,
         sbintime_t sbt, sbintime_t pr, int flags);
int  cv_timedwait_sig_sbt(struct cv *cvp, struct mtx *mtx,
         sbintime_t sbt, sbintime_t pr, int flags);
int  msleep_sbt(void *chan, struct mtx *mtx, int pri,
         const char *wmesg, sbintime_t sbt, sbintime_t pr, int flags);
```

The `sbt` argument is the timeout, expressed as an `sbintime_t` (a 64-bit integer where the upper 32 bits are seconds and the lower 32 bits are a binary fraction of a second). The `pr` argument is the precision: how much wiggle the kernel is allowed to have when scheduling the timer (used for power-saving coalescing of timer interrupts). The `flags` argument is one of `C_HARDCLOCK`, `C_ABSOLUTE`, `C_DIRECT_EXEC`, etc., which control how the timer is registered.

For a 250-microsecond timeout:

```c
sbintime_t sbt = 250 * SBT_1US;  /* 250 microseconds */
int err = cv_timedwait_sig_sbt(&sc->data_cv, &sc->mtx, sbt,
    SBT_1US, C_HARDCLOCK);
```

The `SBT_1US` constant (defined in `/usr/src/sys/sys/time.h`) is one microsecond as an `sbintime_t`. Multiplying by 250 gives 250 microseconds. The precision argument `SBT_1US` says "I am happy with one-microsecond precision"; the kernel will not coalesce this timer with other timers more than 1 microsecond apart.

For 5 seconds:

```c
sbintime_t sbt = 5 * SBT_1S;
int err = cv_timedwait_sig_sbt(&sc->data_cv, &sc->mtx, sbt,
    SBT_1MS, C_HARDCLOCK);
```

Five-second wait with millisecond precision. The kernel may coalesce up to 1 ms.

For most driver code the millisecond-tick API (`cv_timedwait_sig` with a tick count) is the right level of precision. Reach for `_sbt` when you have a real reason: a network protocol with sub-millisecond timing, a hardware controller with a microsecond-scale watchdog, a measurement where the sleep itself contributes to the result.

### What Happens Inside cv_timedwait_sig

Conceptually, `cv_timedwait_sig` does the same thing as `cv_wait_sig` but also schedules a callout that will fire the cv's signal if no real signal arrives first. The implementation lives in `/usr/src/sys/kern/kern_condvar.c` in `_cv_timedwait_sig_sbt`. Three observations are worth carrying with you.

First, the callout is registered while the interlock mutex is held, then the thread sleeps. If the callout fires while the thread is sleeping, the kernel marks the thread as awake-with-timeout. The thread returns from the sleep with `EWOULDBLOCK`.

Second, if a real `cv_signal` arrives before the timeout, the callout is cancelled when the thread wakes. The cancellation is racy in principle (the callout could fire just after the thread wakes for a real reason), but the kernel handles this by checking whether the thread is still sleeping when the callout fires; if not, the callout is a no-op.

Third, every timed wait creates and tears down a callout. On a system with thousands of concurrent timed waits, the callout machinery becomes a measurable cost. For a single driver with at most a few dozen waiters, the cost is negligible.

These details are not things you need to memorize. They explain, however, why a driver that uses timed waits everywhere may show more activity in the callout subsystem than a driver that uses indefinite waits with a separate watchdog thread. If you ever wonder why your driver is producing many callout events, the timed waits are a likely cause.

### Wrapping Up Section 3

Bounded waits and interruptible waits are the two ways a kernel sleep primitive cooperates with the world outside it. We added both to the `myfirst` blocking paths: `cv_wait_sig` was already there; `cv_timedwait_sig` is the new addition, gated by a sysctl. The user-space test confirms that a Ctrl-C and a 100-millisecond deadline both produce the expected return value; the driver reports `EINTR` and `EAGAIN` respectively.

Section 4 turns to a different shape of synchronization entirely: shared/exclusive locks, where many threads may read at once and only writers must wait their turn.



## Section 4: The sx(9) Lock: Shared and Exclusive Access

The mutex `myfirst` uses today protects the cbuf, which has a composite invariant. That is the right primitive for that job. Not every piece of state has a composite invariant, however. Some state is read often, written rarely, and never spans more than the field being read. For that state, serializing every reader through a mutex is wasted serialization. A reader-writer lock fits the shape better.

This section introduces `sx(9)`, FreeBSD's sleepable shared/exclusive lock. We first explain what shared/exclusive means and why it matters, then walk through the API, then talk briefly about the spin-mode sibling `rw(9)`, and finish with the rules that distinguish the two and place each in the right context.

### What Shared and Exclusive Mean

A **shared lock** (also called a *read lock*) allows multiple holders simultaneously. A thread that holds the lock in shared mode is guaranteed that no other thread is currently holding it in *exclusive* mode. Shared holders may execute concurrently; they do not see each other.

An **exclusive lock** (also called a *write lock*) is held by exactly one thread at a time. A thread holding the lock in exclusive mode is guaranteed that no other thread holds it in any mode.

A lock can transition between modes in two directions:

- **Downgrade**: a holder of an exclusive lock can convert it to a shared lock without releasing it. The conversion is non-blocking; immediately after, the original holder still has the lock (now in shared mode), and other readers may proceed.
- **Upgrade**: a holder of a shared lock can attempt to convert it to an exclusive lock. The attempt may fail if other shared holders are still present. The standard primitive is `sx_try_upgrade`, which returns success/failure rather than blocking.

The asymmetry of the upgrade (try, may fail) reflects a fundamental difficulty: if multiple shared holders both try to upgrade at once, they would deadlock waiting for each other. The non-blocking `sx_try_upgrade` lets one succeed while the others fail and have to release-and-reacquire as exclusive.

Shared/exclusive locks are the right primitive when the access pattern is *many readers, occasional writer*. Examples in the FreeBSD kernel include the namespace lock for sysctl, the namespace lock for kernel modules, the superblock locks of file systems, and many configuration-state locks in network drivers.

### Why Shared/Exclusive Beats a Plain Mutex Here

Imagine a piece of driver state, "current debug verbosity level", read at the start of every I/O call to decide whether to log certain events, and changed maybe once an hour by a sysctl. Under the Chapter 11 mutex design:

- Every I/O call acquires the mutex, reads the verbosity, releases the mutex.
- Every I/O call serializes on the mutex against every other I/O call's verbosity check.
- The mutex sees enormous contention even though no one is contending for the underlying *state* (everyone is just reading).

Under an `sx` design:

- Every I/O call acquires the lock in shared mode (cheap on a multi-core system; fast-path reduces to a few atomic operations and no scheduler involvement).
- Multiple I/O calls can hold the lock concurrently. They do not block each other.
- The sysctl writer occasionally takes the lock in exclusive mode, briefly excluding readers. Readers retry as shared holders once the writer drops.

For a read-heavy workload, the difference is dramatic. The mutex's serialization cost grows with the number of cores; the sx's shared-mode cost stays constant.

The trade-off: `sx_xlock` is more expensive per acquisition than `mtx_lock`, because the lock is more elaborate internally. For state that is only ever read once and the readers are not contending, `mtx` is still better. The break-even point depends on the workload, but the rule of thumb is *use sx when readers are many and writers are rare; use mtx when the access pattern is symmetric or write-heavy*.

### The sx(9) API

The sx(9) functions live in `/usr/src/sys/sys/sx.h` and `/usr/src/sys/kern/kern_sx.c`. The public API is small.

```c
void  sx_init(struct sx *sx, const char *description);
void  sx_init_flags(struct sx *sx, const char *description, int opts);
void  sx_destroy(struct sx *sx);

void  sx_xlock(struct sx *sx);
int   sx_xlock_sig(struct sx *sx);
void  sx_xunlock(struct sx *sx);
int   sx_try_xlock(struct sx *sx);

void  sx_slock(struct sx *sx);
int   sx_slock_sig(struct sx *sx);
void  sx_sunlock(struct sx *sx);
int   sx_try_slock(struct sx *sx);

int   sx_try_upgrade(struct sx *sx);
void  sx_downgrade(struct sx *sx);

void  sx_unlock(struct sx *sx);  /* polymorphic: shared or exclusive */
void  sx_assert(struct sx *sx, int what);

int   sx_xlocked(struct sx *sx);
struct thread *sx_xholder(struct sx *sx);
```

The `_sig` variants are interruptible by signals; they return `EINTR` or `ERESTART` if signalled while waiting. The non-`_sig` variants block uninterruptibly. Drivers that hold an sx lock across a long operation should consider the `_sig` variants for the same reason they prefer `cv_wait_sig` over `cv_wait`: a Ctrl-C should be able to release the wait.

The flags accepted by `sx_init_flags` include:

- `SX_DUPOK`: allow the same thread to acquire the lock multiple times (mostly a `WITNESS` directive).
- `SX_NOWITNESS`: do not register the lock with `WITNESS` (use rarely; prefer to register and document any exceptions).
- `SX_RECURSE`: allow recursive acquisition by the same thread; the lock is released only when every acquire is matched.
- `SX_QUIET`, `SX_NOPROFILE`: turn off various debugging instrumentation.
- `SX_NEW`: declare that the memory is fresh (skip the prior-init check).

For most driver use cases, `sx_init(sx, "name")` with no flags is the right default.

`sx_assert(sx, what)` checks the lock state and panics under `INVARIANTS` if the assertion fails. The `what` argument is one of:

- `SA_LOCKED`: the lock is held in some mode by the calling thread.
- `SA_SLOCKED`: the lock is held in shared mode.
- `SA_XLOCKED`: the lock is held in exclusive mode by the calling thread.
- `SA_UNLOCKED`: the lock is not held by the calling thread.
- `SA_RECURSED`, `SA_NOTRECURSED`: matches the recursion state.

Use `sx_assert` liberally inside helpers that expect a particular lock state, in the same way Chapter 11 used `mtx_assert`.

### A Quick Worked Example

Suppose we have a struct holding driver configuration:

```c
struct myfirst_config {
        int     debug_level;
        int     soft_byte_limit;
        char    nickname[32];
};
```

Most reads of these fields happen on the data path (every `myfirst_read` and `myfirst_write` checks `debug_level`). Writes happen rarely, from sysctl handlers.

Add an sx lock to the softc:

```c
struct sx               cfg_sx;
struct myfirst_config   cfg;
```

Initialize and destroy:

```c
sx_init(&sc->cfg_sx, "myfirst cfg");
/* in detach: */
sx_destroy(&sc->cfg_sx);
```

Read on the data path:

```c
static bool
myfirst_debug_enabled(struct myfirst_softc *sc, int level)
{
        bool enabled;

        sx_slock(&sc->cfg_sx);
        enabled = (sc->cfg.debug_level >= level);
        sx_sunlock(&sc->cfg_sx);
        return (enabled);
}
```

Write from a sysctl handler:

```c
static int
myfirst_sysctl_debug_level(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        int new, error;

        sx_slock(&sc->cfg_sx);
        new = sc->cfg.debug_level;
        sx_sunlock(&sc->cfg_sx);

        error = sysctl_handle_int(oidp, &new, 0, req);
        if (error || req->newptr == NULL)
                return (error);

        if (new < 0 || new > 3)
                return (EINVAL);

        sx_xlock(&sc->cfg_sx);
        sc->cfg.debug_level = new;
        sx_xunlock(&sc->cfg_sx);
        return (0);
}
```

Three things to notice in the writer.

First, we read the *current* value under the shared lock so the sysctl framework can fill in the value to display when no new value is being set. We could read it without the lock, but doing so would create a torn-read possibility for the (admittedly small) `int`. The shared lock is cheap and explicit.

Second, we drop the shared lock, validate the new value with `sysctl_handle_int`, validate the range, and then take the exclusive lock to commit. We cannot upgrade from shared to exclusive in this path because `sx_try_upgrade` may fail; doing it as drop-and-reacquire is simpler and correct.

Third, the validation happens before the exclusive lock, which means we hold the exclusive lock for the minimum time. The exclusive holder excludes all readers; we want it to release as quickly as possible.

### Try-Upgrade and Downgrade

`sx_try_upgrade` is the optimistic version of "I have a shared lock, please give me an exclusive one without making me drop and reacquire". It returns nonzero on success (the lock is now exclusive) and zero on failure (the lock is still shared; another thread held it shared simultaneously and the kernel could not safely promote).

The pattern:

```c
sx_slock(&sc->cfg_sx);
/* do some reading */
if (need_to_modify) {
        if (sx_try_upgrade(&sc->cfg_sx)) {
                /* now exclusive; modify */
                sx_downgrade(&sc->cfg_sx);
                /* back to shared; continue reading */
        } else {
                /* upgrade failed; drop and reacquire */
                sx_sunlock(&sc->cfg_sx);
                sx_xlock(&sc->cfg_sx);
                /* now exclusive but our prior view may be stale */
                /* re-validate and modify */
                sx_downgrade(&sc->cfg_sx);
        }
}
sx_sunlock(&sc->cfg_sx);
```

`sx_downgrade` always succeeds: an exclusive holder can always step down to shared without blocking, because no other writer can be present (we held the exclusive) and the existing readers all acquired their shared locks while we were holding exclusive (which they could not), so they cannot exist either.

For our `myfirst` configuration, we do not need upgrade/downgrade: the read and the write are separate paths and the sysctl handler is willing to drop and reacquire. Upgrade/downgrade is most useful in algorithms where the same thread reads, decides, and then conditionally modifies, all within one lock acquire-release cycle.

### Comparing sx(9) to rw(9)

`rw(9)` is the spin-mode sibling of `sx(9)`. Both implement the shared/exclusive idea. They differ in how they wait for an unavailable lock.

`sx(9)` uses sleep queues. A thread that cannot acquire the lock immediately is put on a sleep queue and yields. Other threads run on the CPU. When the lock becomes available, the kernel wakes the highest-priority waiter, which then re-tries.

`rw(9)` uses turnstiles, the kernel's spin-based primitive that supports priority propagation. A thread that cannot acquire the lock immediately spins (briefly), then handed to the turnstile machinery for blocking with priority inheritance. The blocking is done in a way that does not give up the CPU as easily as `sx`.

The practical differences:

- `sx(9)` is sleepable in the strict sense: holding an `sx` lock allows you to call functions that may sleep (`uiomove`, `malloc(... M_WAITOK)`). Holding an `rw(9)` lock does *not*; the `rw(9)` lock is treated like a spin lock for sleep purposes.
- `sx(9)` supports `_sig` variants for signal-interruptible waits. `rw(9)` does not.
- `sx(9)` is generally appropriate for code in process context; `rw(9)` is more appropriate when the critical section is short and may run in interrupt context (although the strict choice for interrupt context is still `MTX_SPIN`).

For `myfirst`, all configuration access is from process context, the critical sections are short but include potentially-sleeping calls, and signal interruption is a useful feature. `sx(9)` is the right choice.

A driver with a configuration that is read inside an interrupt handler would have to use `rw(9)` instead, because `sx_slock` could sleep and sleeping in an interrupt is illegal. We will not encounter such a driver in this book until later parts.

### The Sleepability Rule, Revisited

Chapter 11 introduced the rule "do not hold a non-sleepable lock across a sleeping operation". With sx and cv on the table, the rule needs a small refinement.

The full rule is: *the lock you hold determines what operations are legal in the critical section.*

- Holding an `MTX_DEF` mutex: most operations are legal. Sleeping is permitted (with `mtx_sleep`, `cv_wait`). `uiomove`, `copyin`, `copyout`, and `malloc(M_WAITOK)` are legal in principle but should be avoided to keep critical sections short. The driver convention is to drop the mutex around any of these.
- Holding an `MTX_SPIN` mutex: very few operations are legal. No sleeping. No `uiomove`. No `malloc(M_WAITOK)`. The critical section must be tiny.
- Holding an `sx(9)` lock (shared or exclusive): like `MTX_DEF`. Sleeping is permitted. The same convention of "drop before sleeping if you can" applies, but the absolute prohibition on sleeping does not.
- Holding an `rw(9)` lock: like `MTX_SPIN`. No sleeping. No long-blocking calls.
- Holding a `cv(9)` (i.e., currently inside `cv_wait`): the underlying interlock mutex was atomically dropped by `cv_wait`; from the point of view of "what is held", you hold nothing.

This refinement says: `sx` is sleepable, `rw` is not. That is the operative difference between them. Choose by which side of the line your critical section needs to be on.

### Lock Order and sx

`WITNESS` tracks lock ordering across all classes: mutexes, sx locks, and rw locks. If your driver acquires an `sx` lock while holding a mutex, that establishes an order: mutex first, sx second. The reverse order from any path is a violation; `WITNESS` will warn.

For `myfirst` Stage 3 (this section), we will hold `sc->mtx` and `sc->cfg_sx` together in some paths. We must declare the order explicitly.

The natural order is *mtx before sx*. Reason: the data path holds `sc->mtx` for the cbuf operations; if it needs to read a configuration value during that critical section, it would acquire `sc->cfg_sx` while still holding `sc->mtx`. The reverse (`cfg_sx` first, `mtx` second) is also possible (a sysctl writer that wanted to update both configuration and trigger an event could acquire `cfg_sx`, then `mtx`), but a driver should pick one order and document it.

Section 5 elaborates this design and codifies the rule.

### A Look at kern_sx.c

If you have a few minutes, open `/usr/src/sys/kern/kern_sx.c` and skim. The fast path of `sx_xlock` is one compare-and-swap on the lock word, exactly the same shape as the fast path of `mtx_lock`. The slow path (in `_sx_xlock_hard`) hands the thread to the sleep queue with priority propagation. The shared-lock path (`_sx_slock_int`) is similar but updates the shared-holder count rather than setting the owner.

What matters for the driver writer is that the fast path is cheap, the slow path is correct, and the API is the same shape as the mutex API you already know. If you can use `mtx_lock`, you can use `sx_xlock`; the new vocabulary is the shared-mode operations and the rules around them.

### A Brief Tour of rw(9)

We have mentioned `rw(9)` several times as the spin-mode sibling of `sx(9)`. Although our driver does not use it, you will encounter it in real FreeBSD source, so a short tour is worth the few minutes.

The API mirrors `sx(9)`:

```c
void  rw_init(struct rwlock *rw, const char *name);
void  rw_destroy(struct rwlock *rw);

void  rw_wlock(struct rwlock *rw);
void  rw_wunlock(struct rwlock *rw);
int   rw_try_wlock(struct rwlock *rw);

void  rw_rlock(struct rwlock *rw);
void  rw_runlock(struct rwlock *rw);
int   rw_try_rlock(struct rwlock *rw);

int   rw_try_upgrade(struct rwlock *rw);
void  rw_downgrade(struct rwlock *rw);

void  rw_assert(struct rwlock *rw, int what);
```

The differences from `sx(9)`:

- The mode names are different: `wlock` (write/exclusive) and `rlock` (read/shared) instead of `xlock` and `slock`. Same idea, different vocabulary.
- There are no `_sig` variants. `rw(9)` cannot be interrupted by signals because it is implemented over turnstiles, not sleep queues.
- A thread holding any `rw(9)` lock cannot sleep. No `cv_wait`, no `mtx_sleep`, no `uiomove`, no `malloc(M_WAITOK)`.
- `rw(9)` supports priority propagation. A thread waiting for an exclusive lock that is held by a lower-priority thread will boost the holder's priority. This is the main reason `rw(9)` exists rather than just being a thin wrapper around `sx(9)`.

The `rw_assert` flags are `RA_LOCKED`, `RA_RLOCKED`, `RA_WLOCKED`, plus the same recursion variants `sx_assert` has.

Where you will see `rw(9)` in the FreeBSD tree:

- The networking stack uses `rw(9)` for several read-mostly tables (routing tables, the address resolution table). Read access happens in the receive path, which runs in network interrupt context where sleeping is forbidden.
- The VFS layer uses it for some namespace caches.
- Various subsystems with hot read paths and rare configuration updates.

For our `myfirst` driver, every cfg access happens in process context, every cfg writer is willing to drop the lock around `sysctl_handle_*` (which sleeps), and we benefit from signal interruptibility. `sx(9)` is the right choice. If you ever need to access the same configuration from an interrupt handler (Chapter 14 will discuss this), the answer is to switch to `rw(9)` and accept the constraint that the cfg writer must do all its work without sleeping.

### A Worked Example with rw(9)

To make the alternative concrete, here is what the cfg path would look like with `rw(9)`. The code is structurally identical except for the API and the absence of signal interruptibility:

```c
/* In the softc: */
struct rwlock           cfg_rw;
struct myfirst_config   cfg;

/* In attach: */
rw_init(&sc->cfg_rw, "myfirst cfg");

/* In detach: */
rw_destroy(&sc->cfg_rw);

/* Read path: */
static int
myfirst_get_debug_level_rw(struct myfirst_softc *sc)
{
        int level;

        rw_rlock(&sc->cfg_rw);
        level = sc->cfg.debug_level;
        rw_runlock(&sc->cfg_rw);
        return (level);
}

/* Write path (sysctl handler): */
static int
myfirst_sysctl_debug_level_rw(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        int new, error;

        rw_rlock(&sc->cfg_rw);
        new = sc->cfg.debug_level;
        rw_runlock(&sc->cfg_rw);

        error = sysctl_handle_int(oidp, &new, 0, req);
        if (error || req->newptr == NULL)
                return (error);

        if (new < 0 || new > 3)
                return (EINVAL);

        rw_wlock(&sc->cfg_rw);
        sc->cfg.debug_level = new;
        rw_wunlock(&sc->cfg_rw);
        return (0);
}
```

Two things to notice. First, `sysctl_handle_int` is *outside* the lock. Calling it inside an `rw(9)` critical section would be illegal because `sysctl_handle_int` may sleep. This is the same discipline we use for the `sx(9)` version, but with `rw(9)` it is mandatory rather than merely advisable. Second, the read paths look identical to the `sx(9)` version; only the function names changed. That is the point of the symmetric API: the mental model carries over.

If our driver someday needs to support an interrupt-context reader of the configuration (perhaps a hardware interrupt handler that wants to know the current debug level), this is the change we would make. For now, `sx(9)` is correct and we stay with it.

### Wrapping Up Section 4

`sx(9)` gives us a way to express "many readers, occasional writer" without serializing every reader. It is sleepable, signal-aware, and follows the same lock-order discipline as the mutex. `rw(9)` is its non-sleepable sibling, useful when the critical section may run in contexts where sleeping is illegal; the worked example above shows the small differences. We use `sx(9)` for `myfirst` because process context and signal interruptibility are both desirable.

Section 5 brings the new primitives together. We add a small configuration subsystem to `myfirst`, decide the lock order between the data path and the configuration path, and verify the design against `WITNESS`.



## Section 5: Implementing a Safe Multi-reader, Single-writer Scenario

The previous three sections introduced primitives in isolation. This section combines them into a coherent driver design. We add a small configuration subsystem to `myfirst`, give it its own sx lock, work out the lock order with respect to the existing data-path mutex, and verify the resulting design against a `WITNESS` kernel.

The configuration subsystem is small on purpose. The point is not to demonstrate a complex feature; it is to demonstrate the lock-order discipline that any driver with more than one lock class must follow.

### The Configuration Subsystem

We add three configurable parameters:

- `debug_level`: an integer 0 to 3. Higher values produce more verbose `dmesg` output from the driver's data path.
- `soft_byte_limit`: an integer. If non-zero, the driver refuses to accept writes that would push the cbuf above this many bytes (it returns `EAGAIN` early). This is a poor man's flow-control knob.
- `nickname`: a short string the driver prints in its log lines. Useful for distinguishing multiple driver instances in `dmesg`.

The struct that holds them:

```c
struct myfirst_config {
        int     debug_level;
        int     soft_byte_limit;
        char    nickname[32];
};
```

Add it to the softc, alongside its sx lock:

```c
struct myfirst_softc {
        /* ... existing fields ... */
        struct sx               cfg_sx;
        struct myfirst_config   cfg;
        /* ... rest ... */
};
```

Initialize and destroy:

```c
/* In attach: */
sx_init(&sc->cfg_sx, "myfirst cfg");
sc->cfg.debug_level = 0;
sc->cfg.soft_byte_limit = 0;
strlcpy(sc->cfg.nickname, "myfirst", sizeof(sc->cfg.nickname));

/* In detach: */
sx_destroy(&sc->cfg_sx);
```

The initial values are set before the cdev is created, so no other thread can observe a half-initialized configuration.

### The Lock Order Decision

The driver now has two lock classes that can be held simultaneously: `sc->mtx` (the cbuf and per-softc state) and `sc->cfg_sx` (the configuration). We must decide which is acquired first when both are needed.

The natural questions to ask:

1. Which path holds each lock most often? The data path holds `sc->mtx` constantly (every `myfirst_read` and `myfirst_write` enters and leaves it). The data path also wants to read `sc->cfg.debug_level` to decide whether to log; that is a `sx_slock(&sc->cfg_sx)`. So the data path already wants both, in the order *mtx first, sx second*.

2. Which path holds the cfg lock and might want the data lock? A sysctl handler that updates the configuration takes `sx_xlock(&sc->cfg_sx)`. Does it ever need `sc->mtx`? In principle, yes: a sysctl handler that resets the byte counters would take both. The cleanest design is to *not* take the data mutex from inside the sx critical section; the sysctl writer stages its work, releases the sx lock, and then takes the data mutex if needed. That keeps the order monotonic.

The decision: **`sc->mtx` is acquired before `sc->cfg_sx` whenever both are held simultaneously.**

The reverse order is forbidden. `WITNESS` will catch any violation.

We document the decision in `LOCKING.md`:

```markdown
## Lock Order

sc->mtx -> sc->cfg_sx

A thread holding sc->mtx may acquire sc->cfg_sx (in either shared or
exclusive mode). A thread holding sc->cfg_sx may NOT acquire sc->mtx.

Rationale: the data path always holds sc->mtx and may need to read
configuration during its critical section. The configuration path
(sysctl writers) does not need to update data-path state while
holding sc->cfg_sx; if it needs to, it releases sc->cfg_sx first and
then acquires sc->mtx separately.
```

### Reading the Configuration on the Data Path

The most frequent access to the configuration is the data path's check of `debug_level` to decide whether to emit a log message. We wrap it in a small helper:

```c
static int
myfirst_get_debug_level(struct myfirst_softc *sc)
{
        int level;

        sx_slock(&sc->cfg_sx);
        level = sc->cfg.debug_level;
        sx_sunlock(&sc->cfg_sx);
        return (level);
}
```

Note that this helper takes only `sc->cfg_sx`, not `sc->mtx`. That is intentional: the helper does not need the data mutex to read the configuration. If it is called from a context that already holds `sc->mtx`, the lock order is satisfied (mtx first, sx second). If it is called from a context that holds nothing, that is fine too.

A debug-aware log macro:

```c
#define MYFIRST_DBG(sc, level, fmt, ...) do {                          \
        if (myfirst_get_debug_level(sc) >= (level))                    \
                device_printf((sc)->dev, fmt, ##__VA_ARGS__);          \
} while (0)
```

Use it on the data path:

```c
MYFIRST_DBG(sc, 2, "read got %zu bytes\n", got);
```

The shared-lock acquisition is the cost of every check. On a multi-core machine this is a few atomic operations; the readers do not contend with each other. On a single-core machine the cost is essentially zero (no other thread can be in the middle of a write).

### Reading the Soft Byte Limit

The same pattern for the soft byte limit, used by `myfirst_write` to decide whether to refuse:

```c
static int
myfirst_get_soft_byte_limit(struct myfirst_softc *sc)
{
        int limit;

        sx_slock(&sc->cfg_sx);
        limit = sc->cfg.soft_byte_limit;
        sx_sunlock(&sc->cfg_sx);
        return (limit);
}
```

Inside `myfirst_write`, before the actual write happens (note that `want` has not been computed yet at this point in the loop), the limit check uses `sizeof(bounce)` as a worst-case proxy: any single iteration writes at most one bounce buffer's worth of bytes, so refusing when `cbuf_used + sizeof(bounce)` would exceed the limit is a conservative early-out:

```c
int limit = myfirst_get_soft_byte_limit(sc);

MYFIRST_LOCK(sc);
if (limit > 0 && cbuf_used(&sc->cb) + sizeof(bounce) > (size_t)limit) {
        MYFIRST_UNLOCK(sc);
        return (uio->uio_resid != nbefore ? 0 : EAGAIN);
}
/* fall through to wait_room and the rest of the iteration */
```

Two acquisitions back to back: the cfg sx for the limit, the mtx for the cbuf check. Notice that we acquire the sx *first* and drop it before taking the mutex. We could in principle hold both (cfg_sx then mtx) but the order would be wrong; the rule says mtx first, sx second. So we acquire each independently. The slight cost of two acquisitions is the price of correctness.

A subtle point: between the cfg_sx release and the mtx acquisition, the limit could change. That is acceptable; the limit is a soft hint, not a hard guarantee. If a sysctl writer raises the limit between our two acquisitions and we still refuse the write, the user will retry and succeed on the second attempt. If the limit is lowered and we proceed with a write that the new limit would have refused, no harm is done because the cbuf has its own hard size limit.

The choice of `sizeof(bounce)` rather than the actual `want` reflects another subtle point: at this stage in the loop, the driver has not yet computed `want` (that requires knowing how much room the cbuf currently has, which requires holding the mutex first). Using `sizeof(bounce)` as the worst-case bound lets the check happen before the room calculation. The companion source file follows exactly this pattern.

### Updating the Configuration: a Sysctl Writer

The writer side, exposed as a sysctl that can read and write `debug_level`:

```c
static int
myfirst_sysctl_debug_level(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        int new, error;

        sx_slock(&sc->cfg_sx);
        new = sc->cfg.debug_level;
        sx_sunlock(&sc->cfg_sx);

        error = sysctl_handle_int(oidp, &new, 0, req);
        if (error || req->newptr == NULL)
                return (error);

        if (new < 0 || new > 3)
                return (EINVAL);

        sx_xlock(&sc->cfg_sx);
        sc->cfg.debug_level = new;
        sx_xunlock(&sc->cfg_sx);
        return (0);
}
```

Walk through the lock acquisitions:

1. `sx_slock` to read the current value (so the sysctl framework can return it on a read-only query).
2. `sx_sunlock` before calling `sysctl_handle_int`, because that function may copy data to and from user space (which can sleep) and we do not want to hold the sx lock across that.
3. After validation, `sx_xlock` to commit the new value.
4. `sx_xunlock` to release.

We never hold `sc->mtx` in this path. The lock order rule is trivially satisfied: this path never has both locks held at once.

Register the sysctl in attach:

```c
SYSCTL_ADD_PROC(&sc->sysctl_ctx, SYSCTL_CHILDREN(sc->sysctl_tree),
    OID_AUTO, "debug_level",
    CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_MPSAFE,
    sc, 0, myfirst_sysctl_debug_level, "I",
    "Debug verbosity level (0-3)");
```

The `CTLFLAG_MPSAFE` flag tells the sysctl framework that our handler is safe to call without acquiring the giant lock; we are. This is the modern default for new sysctl handlers.

### Updating the Soft Byte Limit

The same shape for the byte limit:

```c
static int
myfirst_sysctl_soft_byte_limit(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        int new, error;

        sx_slock(&sc->cfg_sx);
        new = sc->cfg.soft_byte_limit;
        sx_sunlock(&sc->cfg_sx);

        error = sysctl_handle_int(oidp, &new, 0, req);
        if (error || req->newptr == NULL)
                return (error);

        if (new < 0)
                return (EINVAL);

        sx_xlock(&sc->cfg_sx);
        sc->cfg.soft_byte_limit = new;
        sx_xunlock(&sc->cfg_sx);
        return (0);
}
```

And for the nickname (a string, so the sysctl handler is slightly different):

```c
static int
myfirst_sysctl_nickname(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        char buf[sizeof(sc->cfg.nickname)];
        int error;

        sx_slock(&sc->cfg_sx);
        strlcpy(buf, sc->cfg.nickname, sizeof(buf));
        sx_sunlock(&sc->cfg_sx);

        error = sysctl_handle_string(oidp, buf, sizeof(buf), req);
        if (error || req->newptr == NULL)
                return (error);

        sx_xlock(&sc->cfg_sx);
        strlcpy(sc->cfg.nickname, buf, sizeof(sc->cfg.nickname));
        sx_xunlock(&sc->cfg_sx);
        return (0);
}
```

The structure is identical: shared-lock to read, drop, validate via the sysctl framework, exclusive-lock to commit. The string version uses `strlcpy` for safety.

### A Single Operation Holding Both Locks

Sometimes a path legitimately needs both locks. As an example, suppose we add a sysctl that resets the cbuf and clears all the byte counters at once. That sysctl needs:

1. The exclusive cfg lock if it is going to also reset some configuration (say, reset the debug level).
2. The data mutex to manipulate the cbuf.

Following our lock order, we acquire `sc->mtx` first, then `sc->cfg_sx`:

```c
static int
myfirst_sysctl_reset(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        int reset = 0;
        int error;

        error = sysctl_handle_int(oidp, &reset, 0, req);
        if (error || req->newptr == NULL || reset != 1)
                return (error);

        MYFIRST_LOCK(sc);
        sx_xlock(&sc->cfg_sx);

        cbuf_reset(&sc->cb);
        sc->cfg.debug_level = 0;
        counter_u64_zero(sc->bytes_read);
        counter_u64_zero(sc->bytes_written);

        sx_xunlock(&sc->cfg_sx);
        MYFIRST_UNLOCK(sc);

        cv_broadcast(&sc->room_cv);  /* room is now available */
        return (0);
}
```

The order of acquisition is `mtx` then `sx`. The order of release is the reverse: `sx` first, `mtx` second. (Releases must reverse the acquisition order to maintain the lock-order invariant for any thread that observes a lock state in the middle.)

The cv broadcast happens after both locks are released. Waking sleepers does not require holding either lock.

`cbuf_reset` is a small helper we add to the cbuf module:

```c
void
cbuf_reset(struct cbuf *cb)
{
        cb->cb_head = 0;
        cb->cb_used = 0;
}
```

It zeroes the indices but does not touch the backing memory; the contents become irrelevant the moment `cb_used` is zero.

### Verifying Against WITNESS

Build the new driver and load it on a `WITNESS` kernel. Run the Chapter 11 stress kit plus a new tester that hammers the sysctls while I/O is happening:

```c
/* config_writer.c: continuously update config sysctls. */
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/sysctl.h>
#include <unistd.h>

int
main(int argc, char **argv)
{
        int seconds = (argc > 1) ? atoi(argv[1]) : 30;
        time_t end = time(NULL) + seconds;
        int v = 0;

        while (time(NULL) < end) {
                v = (v + 1) % 4;
                if (sysctlbyname("dev.myfirst.0.debug_level",
                    NULL, NULL, &v, sizeof(v)) != 0)
                        warn("sysctl debug_level");

                int limit = (v == 0) ? 0 : 4096;
                if (sysctlbyname("dev.myfirst.0.soft_byte_limit",
                    NULL, NULL, &limit, sizeof(limit)) != 0)
                        warn("sysctl soft_byte_limit");

                usleep(10000);  /* 10 ms */
        }
        return (0);
}
```

Run `mp_stress` in one terminal, `mt_reader` in a second, `config_writer` in a third, all simultaneously. Watch `dmesg` for warnings.

Three things should happen:

1. All tests pass with consistent byte counts.
2. The debug level changes visibly in `dmesg` (when the level is high, the data path emits log messages; when low, it is quiet).
3. `WITNESS` is silent. No lock-order reversals reported.

If `WITNESS` does report a reversal, it means the lock order was violated somewhere. Re-read the affected code path against the rule (mtx first, sx second) and fix the violation.

### What Goes Wrong if You Get the Order Backwards

To make the rule concrete, deliberately violate it. Take an existing path that holds `sc->mtx` and rewrite it to acquire the locks in the wrong order. For example, in `myfirst_read`:

```c
/* WRONG: this is the bug we want WITNESS to catch. */
sx_slock(&sc->cfg_sx);   /* sx first */
MYFIRST_LOCK(sc);        /* mtx second; reverses the global order */
/* ... */
MYFIRST_UNLOCK(sc);
sx_sunlock(&sc->cfg_sx);
```

Build, load on a `WITNESS` kernel, run the test kit. `WITNESS` should fire on the first run that exercises both this path and any other path that does mtx-then-sx:

```text
lock order reversal:
 1st 0xfffffe000a1b2c30 myfirst cfg (myfirst cfg, sx) @ ...:<line>
 2nd 0xfffffe000a1b2c50 myfirst0 (myfirst, sleep mutex) @ ...:<line>
lock order myfirst cfg -> myfirst0 attempted at ...
where myfirst0 -> myfirst cfg is established at ...
```

The warning names the two locks, their addresses, the source location of each acquisition, and the previously-established order. The fix is to put the locks back in the canonical order; revert the change and the warning disappears.

This is `WITNESS` doing its job. A driver with more than one lock class without `WITNESS` is a driver waiting for a deadlock that nobody can reproduce.

### A Slightly Larger Pattern: Snapshot-and-Apply

A common pattern when both locks are needed and the operation has work to do under each is *snapshot-and-apply*. In its simplest form, before the actual transfer size is known:

```c
/* Phase 1: snapshot the configuration under the sx (in shared mode). */
sx_slock(&sc->cfg_sx);
int dbg = sc->cfg.debug_level;
int limit = sc->cfg.soft_byte_limit;
sx_sunlock(&sc->cfg_sx);

/* Phase 2: do the work under the mtx, using the snapshot. */
MYFIRST_LOCK(sc);
if (limit > 0 && cbuf_used(&sc->cb) + sizeof(bounce) > (size_t)limit) {
        MYFIRST_UNLOCK(sc);
        return (EAGAIN);
}
/* ... cbuf operations ... */
size_t actual = /* determined inside the critical section */;
MYFIRST_UNLOCK(sc);

if (dbg >= 2)
        device_printf(sc->dev, "wrote %zu bytes\n", actual);
```

The snapshot-and-apply pattern keeps each lock held for the minimum time, avoids holding both locks at once, and produces a clear two-phase shape that is easy to reason about. The cost is that the snapshot may be slightly stale when the apply executes; in practice, the staleness is microseconds and is acceptable for almost any configuration value.

If staleness is unacceptable for some specific value (say, a critical security flag), then the holder must take the lock(s) atomically and follow the global order. The snapshot pattern is a default, not a law.

### A Walk-Through of myfirst_sysctl_reset

The reset sysctl is the only path in the chapter that legitimately holds both locks at once. It is worth tracing through carefully, because the pattern (`mtx` then `sx`, both released in reverse order, broadcasts after both are released) is the one to imitate any time both locks must be held.

When a user runs `sysctl -w dev.myfirst.0.reset=1`, the kernel calls `myfirst_sysctl_reset` with `req->newptr` non-NULL. The handler:

1. Reads the new value via `sysctl_handle_int`. If the value is not 1, returns without doing anything (treats only `1` as a confirmed reset request).
2. Acquires `MYFIRST_LOCK(sc)`. The data mutex is now held.
3. Acquires `sx_xlock(&sc->cfg_sx)`. The cfg sx is now held in exclusive mode. Both locks held; lock order satisfied (mtx first, sx second).
4. Calls `cbuf_reset(&sc->cb)`. The buffer is now empty (`cb_used = 0`, `cb_head = 0`).
5. Sets `sc->cfg.debug_level = 0`. The config is now in its initial state.
6. Calls `counter_u64_zero` on each per-CPU counter. The byte counters are now zero.
7. Calls `sx_xunlock(&sc->cfg_sx)`. The cfg sx is released. Only the mtx is held now.
8. Calls `MYFIRST_UNLOCK(sc)`. The mtx is released. No locks held.
9. Calls `cv_broadcast(&sc->room_cv)`. Any writers blocked on full are woken; they will recheck `cb_free`, find it equal to `cb_size`, and proceed.
10. Returns 0.

Three observations about this sequence.

The lock acquisitions follow the global order; `WITNESS` is happy. The releases follow the reverse order, which preserves the invariant that any thread observing the sequence sees a consistent state.

The broadcasts happen *after* both locks are released. Holding either lock while broadcasting would needlessly block the awakened threads when they try to acquire what they need; the broadcast is fire-and-forget, and the kernel handles the rest.

The reset of the data path (`cbuf_reset`) and the reset of the configuration (`debug_level = 0`) are atomic with respect to each other. A thread that observes any field after the reset sees the post-reset value of every field; no thread can observe a half-reset state.

If we wanted to add more reset operations (clear a state machine, reset a hardware register), each one fits into this template. The lock holds expand to cover the new fields; the broadcasts at the end notify the appropriate cvs.

### Wrapping Up Section 5

The driver now has two lock classes: a mutex for the data path and an sx lock for the configuration. The order is documented (mtx first, sx second), the order is enforced by `WITNESS` at runtime, and the patterns we use (snapshot-and-apply for the common case, single-acquisition for paths that need only one lock, and the carefully-ordered acquisition-release for paths that genuinely need both) keep the design auditable. The new sysctls let user space adjust the driver's behaviour at runtime without disrupting the data path.

The bigger lesson of Section 5 is that adding a second lock class is a real design decision, not a transparent optimization. The cost is the discipline required to maintain lock order, the documentation effort, and the audit work to verify both at runtime. The benefit is that the data path is no longer the only place state lives; the configuration has its own protection, its own pace, and its own sysctl interface. As drivers grow, they typically end up with three or four lock classes, each with its own purpose. The pattern from this section scales.

Section 6 turns to the question that always comes next when you start mixing lock classes: how do you debug a synchronization bug when it appears? The kernel gives you several tools for the job; this is the chapter that introduces them.



## Section 6: Debugging Synchronization Issues

Chapter 11 introduced the basic kernel debugging hooks: `INVARIANTS`, `WITNESS`, `KASSERT`, and `mtx_assert`. We used them to verify that the mutex was held at the right times and that lock-order rules were respected. With the broader synchronization toolkit Chapter 12 has put in your hands, the debugging tools also expand. This section walks through the patterns and the tools you will use most often: reading a `WITNESS` warning carefully, inspecting a hung system with the in-kernel debugger, and recognising the most common failure modes of condition variables and shared/exclusive locks.

### A Catalogue of Synchronization Bugs

Six failure shapes cover most of what you will encounter in practice. Recognising the shape is half the diagnosis.

**Lost wakeup.** A thread enters `cv_wait_sig` (or `mtx_sleep`); the condition it is waiting for becomes true but no `cv_signal` (or `wakeup`) is ever called. The thread sleeps forever. Causes: forgot the `cv_signal` after the state change; signalled the wrong cv; signalled before the state was actually changed; the state change happens in a path that does not include a signal at all.

**Spurious wakeup mishandled.** A thread is awakened by a signal or other transient event but the condition it was waiting for is still false. If the surrounding code does not loop and re-check, the thread proceeds with the assumption that the condition is true and operates on stale state. Cure: always wrap `cv_wait` calls in `while (!condition) cv_wait(...)`, never `if (!condition) cv_wait(...)`.

**Lock-order reversal.** Two locks acquired in opposite orders by two paths. Either deadlocks under contention or `WITNESS` catches it. Cure: define a global order in `LOCKING.md` and follow it everywhere.

**Premature destroy.** A cv or sx is destroyed while a thread is still waiting on it. Symptoms are unpredictable: panics, use-after-free, stale-pointer crashes. Cure: ensure every waiter has been woken (and has actually returned from the wait primitive) before calling `cv_destroy` or `sx_destroy`. The detach path must be careful here.

**Sleeping with a non-sleepable lock held.** Holding an `MTX_SPIN` mutex or an `rw(9)` lock and then calling `cv_wait`, `mtx_sleep`, `uiomove`, or `malloc(M_WAITOK)`. `WITNESS` catches it; on a non-`WITNESS` kernel the system either deadlocks or panics. Cure: drop the spin/rw lock before sleeping, or hold an `MTX_DEF` mutex / `sx(9)` lock instead, both of which permit sleeping.

**Race between detach and an active operation.** A descriptor is open, a thread is in `cv_wait_sig`, and the device is told to detach. The detach path must wake the sleeper, must wait until the sleeper has returned, and must keep the cv (and the mutex) alive until that has happened. Cure: the standard detach pattern is set the "going away" flag, broadcast all cvs, wait for `active_fhs` to drop to zero, then destroy primitives.

A driver that survives long enough to hit production has handled each of these at least once. The labs in this chapter let you provoke and resolve several deliberately so the patterns become familiar.

### Reading a WITNESS Warning Carefully

A `WITNESS` warning has three useful parts: the warning text, the source locations of each lock acquisition, and the previously-established order. Pull them apart.

A typical warning for a lock-order reversal:

```text
lock order reversal:
 1st 0xfffffe000a1b2c30 myfirst cfg (myfirst cfg, sx) @ /var/.../myfirst.c:120
 2nd 0xfffffe000a1b2c50 myfirst0 (myfirst, sleep mutex) @ /var/.../myfirst.c:240
lock order myfirst cfg -> myfirst0 attempted at /var/.../myfirst.c:241
where myfirst0 -> myfirst cfg is established at /var/.../myfirst.c:280
```

Reading from top to bottom:

- **`lock order reversal:`**: the warning class. Other classes include `acquiring duplicate lock of same type`, `sleeping thread (pid N) owns a non-sleepable lock`, `WITNESS exceeded the recursion limit`.
- **`1st 0x... myfirst cfg`**: the first lock acquired in the offending path. The address (`0x...`), name (`myfirst cfg`), type (`sx`), and source location (`myfirst.c:120`) tell you exactly which lock and where.
- **`2nd 0x... myfirst0`**: the second lock acquired. Same set of fields.
- **`lock order myfirst cfg -> myfirst0 attempted at ...`**: the order this code is trying to use.
- **`where myfirst0 -> myfirst cfg is established at ...`**: the order `WITNESS` previously observed and recorded as canonical, with the source location of the canonical example.

The fix is one of two things. Either the new path is wrong and should follow the canonical order (most common), or the canonical order itself is wrong and needs to change. Picking which is a judgement call; usually the answer is to fix the new path to match the canonical, because the canonical was probably right.

Sometimes the warning is a false alarm: two distinct objects of the same lock type, where the order between them does not matter because they are independent. `WITNESS` does not always know this; the `LOR_DUPOK` flag at lock initialization tells it to skip the check. We do not need that for `myfirst`, but real drivers with per-instance locks sometimes do.

### Using DDB to Inspect a Hung System

If the test hangs and `dmesg` is silent, the culprit is usually a missed wakeup or a deadlock. The in-kernel debugger (DDB) lets you inspect the state of every thread and every lock at the moment of the hang.

To enter DDB on a hung system, press the `Break` key on the console (or send the `~b` escape if you are on a serial console with `cu`). DDB prompts you with `db>`.

The most useful commands for synchronization debugging:

- `show locks`: list the locks held by the current thread (the one DDB came in on, which is usually the kernel idle thread; rarely useful by itself).
- `show all locks` (alias `show alllocks`): list every lock held by every thread on the system. This is the command you want most of the time.
- `show witness`: dump the entire `WITNESS` lock-order graph. Verbose but authoritative.
- `show sleepchain <thread>`: trace the chain of locks and waits a specific thread is involved in. Useful when you suspect a deadlock loop.
- `show lockchain <lock>`: trace from a lock to the thread that holds it and any other locks that thread holds.
- `ps`: list all processes and threads with their state.
- `bt <thread>`: backtrace of a specific thread.
- `continue`: leave DDB and resume the system. Use only if you have not made any changes.
- `panic`: force a panic so the system reboots cleanly. Use if `continue` is not safe.

Workflow for a hung test:

1. Enter DDB via the console break.
2. `show all locks`. Note any threads holding locks.
3. `ps` to find the test threads (look for `myfrd`, `myfwr`, or `cv_w` in the wait channel column).
4. For each interesting thread, `bt <pid> <tid>` to get a backtrace.
5. If a deadlock is suspected, `show sleepchain` for each waiting thread.
6. Once you have enough information, `panic` to reboot.

The DDB transcript becomes your debugging log. Save it (DDB can output to the console and the kernel message buffer; on a debug kernel with `EARLY_AP_STARTUP`, you can redirect output to serial).

### Recognising Lost Wakeups

A lost wakeup is the most common cv bug. The symptom is a hung waiter that has been there forever; the thread is in `cv_wait_sig` and nothing wakes it.

Detection in DDB:

```text
db> ps
... (find the hung thread, in state "*myfirst data" for example)
db> bt 1234 1235
... (backtrace shows the thread inside cv_wait_sig)
db> show locks
... (the thread holds no locks; it is sleeping)
```

The cv in question is identified by the wait channel name. If the channel name is `myfirst data`, the cv is `sc->data_cv`. Now ask: who should have called `cv_signal(&sc->data_cv)`? Search the source:

```sh
$ grep -n 'cv_signal(&sc->data_cv\|cv_broadcast(&sc->data_cv' myfirst.c
```

For each call site, work out whether it was supposed to fire and whether it actually fired. Common culprits:

- The signal is inside an `if` that was never true.
- The signal is after a `return` that bypassed it.
- The signal targets the wrong cv (`cv_signal(&sc->room_cv)` instead of `data_cv`).
- The state change does not actually change the condition (you incremented `cb_used` but the consumer was checking `cb_free`, which is the same logical state but `cb_free == cb_size - cb_used`; that one is fine, but a similar miscalculation can hide).

Fix the source, rebuild, retest. The symptom should disappear.

### Recognising Spurious Wakeups

A spurious wakeup is a wakeup that arrives while the condition is still false. Causes include signals (`cv_wait_sig` returns due to a signal even when the condition has not changed) and timeouts (`cv_timedwait_sig` returns due to the timer). Both are normal; the driver must handle them.

Detection: the bug is *not* the wakeup itself but the failure to handle it. The shape:

```c
/* WRONG: */
if (cbuf_used(&sc->cb) == 0)
        cv_wait_sig(&sc->data_cv, &sc->mtx);
/* now reading cbuf assuming there is data, but a spurious wakeup
   could have brought us here while the buffer is still empty */
got = cbuf_read(&sc->cb, bounce, take);
```

`cbuf_read` would return zero in that case, which propagates to user space as a zero-byte read, which `cat` and other utilities interpret as EOF. The user sees a silent end-of-file that is not really an end of file.

Cure: always loop:

```c
/* CORRECT: */
while (cbuf_used(&sc->cb) == 0) {
        int error = cv_wait_sig(&sc->data_cv, &sc->mtx);
        if (error != 0)
                return (error);
        if (!sc->is_attached)
                return (ENXIO);
}
got = cbuf_read(&sc->cb, bounce, take);
```

The `myfirst_wait_data` helper from Section 2 already follows this pattern. The general rule is: *never use `if` around a `cv_wait`; always use `while`.*

### Recognising Lock Order Reversal

We saw a `WITNESS` warning earlier. The alternative, on a non-`WITNESS` kernel, is a deadlock. Two threads each hold one lock and want the other; neither can proceed.

Detection in DDB:

```text
db> show all locks
Process 1234 (test1) thread 0xfffffe...
shared sx myfirst cfg (myfirst cfg) ... locked @ ...:120
shared sx myfirst cfg (myfirst cfg) r = 1 ... locked @ ...:120

Process 5678 (test2) thread 0xfffffe...
exclusive sleep mutex myfirst0 (myfirst) r = 0 ... locked @ ...:240
```

Then `show sleepchain` for each waiting thread:

```text
db> show sleepchain 1234
Thread 1234 (pid X) blocked on lock myfirst0 owned by thread 5678
db> show sleepchain 5678
Thread 5678 (pid Y) blocked on lock myfirst cfg owned by thread 1234
```

That cycle is the deadlock. Each thread is blocked on a lock held by the other. The fix is to revisit the lock order; one of the two paths is acquiring in the wrong order. The fix is the same as for the `WITNESS` warning: find the offending acquisition and reorder it.

### Sleeping With a Non-Sleepable Lock

If your driver uses `MTX_SPIN` mutexes or `rw(9)` locks and somewhere you call `cv_wait`, `mtx_sleep`, `uiomove`, or `malloc(M_WAITOK)` while holding one, `WITNESS` will fire:

```text
sleeping thread (pid 1234, tid 5678) owns a non-sleepable lock:
exclusive rw lock myfirst rw (myfirst rw) r = 0 ... locked @ ...:100
```

The warning names the lock and the location of the acquisition. The fix is to drop the lock before the sleeping operation. `myfirst` does not use `MTX_SPIN` or `rw(9)`, so we will not encounter this directly; if you reuse the patterns in another driver, watch for it.

### Premature Destroy

A cv or sx that is destroyed while a thread is still waiting causes use-after-free style crashes. The symptom is usually a panic with a backtrace inside `cv_wait` or `sx_xlock` after `cv_destroy` or `sx_destroy` has already run.

The Chapter 11 detach pattern (refuse detach while `active_fhs > 0`) prevents this for most situations. The Chapter 12 driver extends the pattern with `cv_broadcast` calls before destroying:

```c
MYFIRST_LOCK(sc);
sc->is_attached = 0;
cv_broadcast(&sc->data_cv);
cv_broadcast(&sc->room_cv);
MYFIRST_UNLOCK(sc);

/* now wait for any thread that was sleeping to return.
   The cv_broadcast above wakes them; they see is_attached
   is false and return ENXIO. They release the mutex on
   their way out. */

/* Once we know no one is in the I/O paths, destroy the
   primitives. By construction, active_fhs == 0 when we get
   here, so no thread can re-enter. */
cv_destroy(&sc->data_cv);
cv_destroy(&sc->room_cv);
sx_destroy(&sc->cfg_sx);
mtx_destroy(&sc->mtx);
```

The order is important. The mutex is the interlock for the cvs (a thread inside `cv_wait_sig` was sleeping with `sc->mtx` released to the kernel; on wake-up, `cv_wait_sig` reacquires `sc->mtx` before it returns). If we destroy `sc->mtx` first and then `cv_destroy`, a not-yet-fully-woken thread can be stuck inside the kernel trying to reacquire a mutex whose memory we have just torn down. Destroying the cvs first guarantees that no thread is still inside `cv_wait_sig` by the time the mutex goes away. The same reasoning applies to the sx: a thread blocked inside `sx_xlock` does not hold the mutex, but its post-wake reacquisition path may trip over the order if the sx and the mutex are torn down simultaneously. Destroy in the reverse of the order in which a thread might still be holding or waiting on each primitive: cvs first (waiters drained), then the sx (no readers or writers left), then the mutex (no interlock partner left).

### Helpful Asserts to Add

Sprinkle `sx_assert` and `mtx_assert` calls throughout the helpers to document the expected lock state. Each is free at production time (`INVARIANTS` is compiled out) and catches new bugs on the debug kernel.

Examples:

```c
static int
myfirst_get_debug_level(struct myfirst_softc *sc)
{
        int level;

        sx_slock(&sc->cfg_sx);
        sx_assert(&sc->cfg_sx, SA_SLOCKED);  /* document the lock state */
        level = sc->cfg.debug_level;
        sx_sunlock(&sc->cfg_sx);
        return (level);
}
```

The `sx_assert(&sc->cfg_sx, SA_SLOCKED)` is technically redundant after `sx_slock` (the lock was just acquired), but it makes the intent obvious to a reader and catches refactoring mistakes (someone moves the function and forgets the lock).

A more useful pattern: assertions in helpers that *expect* the caller to have acquired a lock:

```c
static void
myfirst_apply_debug_level(struct myfirst_softc *sc, int level)
{
        sx_assert(&sc->cfg_sx, SA_XLOCKED);  /* caller must hold xlock */
        sc->cfg.debug_level = level;
}
```

If a future call site tries to use this helper without the lock, the assertion fires. The function's expectations are now executable, not just documented.

### Tracing Lock Activity With dtrace and lockstat

Two user-space tools let you observe lock behaviour without modifying the driver.

`lockstat(1)` summarises lock contention over a period:

```sh
# lockstat -P sleep 10
```

This runs for 10 seconds and prints a table of every lock that was contended, with hold times and wait times. For `myfirst` under `mp_stress`, you should see `myfirst0` (the device mutex) and `myfirst cfg` (the sx) at the top of the list. Whether either has contention worth worrying about depends on the workload; for our pseudo-device, neither should.

`dtrace(1)` lets you trace specific events. To see every cv_signal on the data cv:

```sh
# dtrace -n 'fbt::cv_signal:entry /args[0]->cv_description == "myfirst data"/ { stack(); }'
```

This prints a kernel stack trace every time the signal is sent. Useful for pinpointing exactly which path is signalling and from where.

Both tools have minimal overhead and can be used on a production kernel as well as a debug one.

### A Worked Walk-Through: A Lost Wakeup Bug

To make the diagnostic workflow concrete, walk through a hypothetical lost-wakeup bug from symptom to fix.

You make a change to the driver that subtly breaks the signal pairing in `myfirst_write`. After the change, the test suite mostly passes, but `mt_reader` occasionally hangs. You can reproduce it by running `mt_reader` ten or twenty times; once or twice the program hangs at one of its threads.

**Step 1: confirm the symptom with `procstat`.**

```sh
$ ps -ax | grep mt_reader
12345 ?? S+    mt_reader

$ procstat -kk 12345
  PID    TID COMM             TDNAME           KSTACK
12345 67890 mt_reader        -                mi_switch+0xc1 _cv_wait_sig+0xff
                                              myfirst_wait_data+0x4e
                                              myfirst_read+0x91
                                              dofileread+0x82
                                              sys_read+0xb5
```

The thread is in `_cv_wait_sig`. Looking up its wait channel:

```sh
$ ps -axHo pid,tid,wchan,command | grep mt_reader
12345 67890 myfirst         mt_reader
12345 67891 -               mt_reader
```

One thread blocked on `myfirst ` (the eight-character truncation of `"myfirst data"`; see Section 2). The others have exited. So the cv `data_cv` has a waiter, and presumably the driver did not signal it when it should have.

**Step 2: check the cbuf state.**

```sh
$ sysctl dev.myfirst.0.stats.cb_used
dev.myfirst.0.stats.cb_used: 17
```

The buffer has 17 bytes in it. The reader should be able to drain those bytes and return them. Why is the reader still asleep?

**Step 3: examine the source.** The reader is in `cv_wait_sig(&sc->data_cv, &sc->mtx)`. Who calls `cv_signal(&sc->data_cv)`? Search:

```sh
$ grep -n 'cv_signal(&sc->data_cv\|cv_broadcast(&sc->data_cv' myfirst.c
180:        cv_signal(&sc->data_cv);
220:        cv_broadcast(&sc->data_cv);  /* in detach */
```

Two callers. The relevant one is line 180, in `myfirst_write`. Look at it:

```c
put = myfirst_buf_write(sc, bounce, want);
fh->writes += put;
MYFIRST_UNLOCK(sc);

if (put > 0) {
        cv_signal(&sc->data_cv);
        selwakeup(&sc->rsel);
}
```

The signal is conditional on `put > 0`. It looks correct. But the bug introduced earlier might have changed something else. Look further up:

```c
MYFIRST_LOCK(sc);
error = myfirst_wait_room(sc, ioflag, nbefore, uio);
if (error != 0) {
        MYFIRST_UNLOCK(sc);
        return (error == -1 ? 0 : error);
}
room = cbuf_free(&sc->cb);
MYFIRST_UNLOCK(sc);

want = MIN((size_t)uio->uio_resid, sizeof(bounce));
want = MIN(want, room);
error = uiomove(bounce, want, uio);
if (error != 0)
        return (error);

MYFIRST_LOCK(sc);
put = myfirst_buf_write(sc, bounce, want);
```

Here is the bug. After `uiomove`, the code jumps straight back into the cbuf write. But what if `want` was computed against a stale `room`? Suppose between the `cbuf_free` call and the second `MYFIRST_LOCK`, another writer added bytes. The second `myfirst_buf_write` could be called with a `want` that exceeds the actual current room.

In our case, `myfirst_buf_write` returns the actual number of bytes written, which may be less than `want`. We update `bytes_written` correctly. But we then signal `data_cv` only if `put > 0`. So far so good.

But wait. Look at the buggy line carefully: imagine the change introduced was to wrap the signal in a different condition:

```c
if (put == want) {  /* WRONG: was put > 0 */
        cv_signal(&sc->data_cv);
        selwakeup(&sc->rsel);
}
```

Now if `put < want` (because another writer beat us to the room), we do not signal. The bytes were added to the cbuf, but the readers are not woken. A reader currently in `cv_wait_sig` will sleep until somebody else writes a complete buffer.

That is the lost-wakeup bug. The fix is to signal whenever `put > 0`, not whenever `put == want`. Apply the fix, rebuild, retest. The hang disappears.

**Step 4: prevent regression.** Add a `KASSERT` at the wakeup site that documents the contract:

```c
KASSERT(put <= want, ("myfirst_buf_write returned %zu > want=%zu",
    put, want));
if (put > 0) {
        cv_signal(&sc->data_cv);
        selwakeup(&sc->rsel);
}
```

The KASSERT does not catch the bug we just fixed (it was triggered by `put != want`, which is permitted). But it documents that the signal-condition is "any forward progress", which is the rule the next maintainer should preserve.

This walk-through is contrived; real bugs are messier. The pattern is real. Symptom -> instrumentation -> source examination -> hypothesis -> fix -> regression guard. Practice it on the labs in this chapter.

### A Worked Walk-Through: a Lock Order Reversal

Another common scenario: `WITNESS` reports a reversal that you do not immediately understand.

The warning, simplified:

```text
lock order reversal:
 1st 0xfffffe000a1b2c30 myfirst cfg (myfirst cfg, sx) @ myfirst.c:120
 2nd 0xfffffe000a1b2c50 myfirst0 (myfirst, sleep mutex) @ myfirst.c:240
lock order myfirst cfg -> myfirst0 attempted at myfirst.c:241
where myfirst0 -> myfirst cfg is established at myfirst.c:280
```

**Step 1: identify the locks.** `myfirst cfg` is the sx; `myfirst0` is the device mutex.

**Step 2: identify the canonical order.** From the warning: `myfirst0 -> myfirst cfg`. So mtx first, then sx.

**Step 3: identify the violating path.** The warning says the violating path is at `myfirst.c:241`, where it tries to acquire `myfirst0` while already holding `myfirst cfg`. Open the source at line 241 and trace upward to find when the cfg sx was acquired (line 120, by the warning's `1st` field).

**Step 4: decide the fix.** Two options. Either reorder the violating path to match the canonical order (acquire mtx first, then sx; this is usually the cheaper change), or accept that the violating path has a real reason to acquire in the reverse order, in which case the canonical order needs to change globally and `LOCKING.md` updated.

For our `myfirst`, the violating path almost certainly should match the canonical. The fix is to read the cfg value via the snapshot-and-apply pattern: drop the sx before acquiring the mtx.

**Step 5: verify.** Apply the fix, rebuild, rerun the test that triggered the warning. `WITNESS` should now be silent. If it is not, the warning has shifted to a different path and you have a second violation to investigate.

### Common Mistakes That Hide From WITNESS

`WITNESS` is excellent at what it checks but does not check everything. Three classes of bug it cannot detect:

**Locks held across function pointers.** If a function holds lock A and calls a callback that the function pointer is supplied by user-controlled configuration, `WITNESS` cannot predict what locks the callback might acquire. The lock order with respect to the callback is undefined. Avoid this pattern; if you must use it, document the acceptable lock states for any callback.

**Race conditions on lockless fields.** A field that is intentionally accessed without a lock is invisible to `WITNESS`. If two threads race on that field and the racing matters, `WITNESS` will not warn. Use atomics or the appropriate lock; never assume a lockless field is safe just because no warning fires.

**Incorrect protection.** A field is protected by a mutex on the write path but read without the mutex on the read path. Intermittent torn reads result. `WITNESS` does not flag this; the audit procedure from Chapter 11 Section 3 does.

The cure for all three is the discipline of writing `LOCKING.md` and keeping it accurate. `WITNESS` confirms that the locks you claim to hold, you actually hold; the document confirms that the rules you claim to follow are the rules the design intends.

### Wrapping Up Section 6

Synchronization debugging is a vocabulary first and a toolset second. The vocabulary is the six failure shapes: lost wakeup, spurious wakeup, lock-order reversal, premature destroy, sleeping with a non-sleepable lock, race between detach and active operation. Each has a recognisable signature and a standard cure. The toolset is `WITNESS`, `INVARIANTS`, `KASSERT`, `sx_assert`, the in-kernel debugger commands `show all locks` and `show sleepchain`, and the user-space observability tools `lockstat(1)` and `dtrace(1)`.

Section 7 puts these tools to work on a realistic stress scenario.



## Section 7: Stress Testing with Realistic I/O Patterns

The Chapter 11 stress kit (`producer_consumer`, `mp_stress`, `mt_reader`, `lat_tester`) tested the data path under simple multi-threaded and multi-process workloads. The Chapter 12 driver has new primitives (cv, sx) and a new code path (the configuration sysctls). This section extends the testing to exercise the new surfaces and shows you how to read the resulting data.

The goal is not exhaustive coverage; it is *realistic* coverage. A driver that survives a stress workload that resembles real production traffic is a driver you can trust.

### What Realistic Looks Like

A real driver typically has:

- Multiple producers and consumers running concurrently.
- Sysctl reads sprinkled throughout (monitoring tools, dashboards).
- Occasional sysctl writes (configuration changes from an administrator).
- Bursts of activity interleaved with idle periods.
- Mixed-priority threads competing for the CPU.

A test that exercises only one of these axes can miss bugs that only appear when several axes interact. For example, a sysctl write that takes the cfg sx in exclusive mode while a data path is mid-operation might expose a subtle ordering issue that pure I/O testing would not.

### A Composite Workload

Build a script that runs three things at once for a fixed duration:

```sh
#!/bin/sh
# Composite stress: I/O + sysctl readers + sysctl writers.

DUR=60

(./mp_stress &) >/tmp/mp.log
(./mt_reader &) >/tmp/mt.log
(./config_writer $DUR &) >/tmp/cw.log

# Burst sysctl readers
for i in 1 2 3 4; do
    (while sleep 0.5; do
        sysctl -q dev.myfirst.0.stats >/dev/null
        sysctl -q dev.myfirst.0.debug_level >/dev/null
        sysctl -q dev.myfirst.0.soft_byte_limit >/dev/null
    done) &
done
SREAD_PIDS=$!

sleep $DUR

# Stop everything
pkill -f mp_stress
pkill -f mt_reader
pkill -f config_writer
kill $SREAD_PIDS 2>/dev/null

wait

echo "=== mp_stress ==="
cat /tmp/mp.log
echo "=== mt_reader ==="
cat /tmp/mt.log
echo "=== config_writer ==="
cat /tmp/cw.log
```

The script runs for one minute. During that minute, the driver sees:

- Two writer processes hammering writes.
- Two reader processes hammering reads.
- Four pthreads in `mt_reader` hammering reads on a single descriptor.
- The `config_writer` toggling debug level and soft byte limit every 10 ms.
- Four shell loops reading sysctls every 0.5 seconds.

Under a debug kernel with `WITNESS`, this is enough activity to catch most lock-ordering and signal-pairing bugs. Run it. If it completes without panic, without `WITNESS` warnings, and with consistent byte counts in the readers and writers, the driver has passed a meaningful synchronization test.

### Long-Duration Variant

For the most subtle bugs, run the composite for an hour:

```sh
$ for i in $(seq 1 60); do
    ./composite_stress.sh
    echo "iteration $i complete"
    sleep 5
  done
```

Sixty iterations of a one-minute test gives an hour of cumulative coverage. Bugs that appear once per million events (which is roughly what an hour of mp_stress produces on a modern machine) usually surface during this run.

### Latency Under Mixed Load

The Chapter 11 `lat_tester` measured the latency of a single read with no other load. Under realistic load, latency tells a different story: it includes time waiting for the mutex, time waiting for the sx, and time inside `cv_wait_sig`.

Run `lat_tester` while `mp_stress` and `config_writer` are running. The histogram should show a longer tail than the unloaded case. A few microseconds for uncontested operations, a few tens of microseconds when the mutex is briefly held by another thread, and a small spike of milliseconds when the cv had to actually sleep waiting for data. If the tail extends to seconds, something is wrong.

### Reading lockstat Output

`lockstat(1)` is the canonical tool for measuring lock contention. Run it during a heavy stress:

```sh
# lockstat -P sleep 30 > /tmp/lockstat.out
```

The `-P` flag includes spin lock data; without it, only adaptive locks are reported. The 30 means "sample for 30 seconds".

The output is organized by lock, with hold time and wait time statistics. For our driver, look for lines mentioning `myfirst0` (the mtx), `myfirst cfg` (the sx), and the cvs (`myfirst data`, `myfirst room`).

A healthy result for `myfirst` under typical stress:

- The mtx is acquired millions of times. Hold time per acquisition is tens of nanoseconds. Wait time is occasional and small.
- The sx is acquired tens of thousands of times. Most acquisitions are shared; the few exclusive acquisitions correspond to sysctl writes. Hold time is low.
- The cvs are signalled and broadcast in proportion to the I/O rate. Wait counts on each cv correspond to the number of times a reader or writer actually had to block.

If any lock shows wait time as a significant fraction of total time, that lock is contended. The fix is one of: shorter critical sections, finer-grained locking, or a different primitive.

For our pseudo-device with a single-mutex design on the data path, the mtx will saturate around 4-8 cores depending on the speed of cbuf operations. This is expected; we have not optimized for high core counts. The point of the chapter is correctness, not throughput.

### Tracing With dtrace

When a specific event needs visibility, `dtrace` is the right tool. Example: count how many times each cv was signalled during a 10-second window:

```sh
# dtrace -n 'fbt::cv_signal:entry { @[args[0]->cv_description] = count(); }' \
    -n 'fbt::cv_broadcastpri:entry { @[args[0]->cv_description] = count(); }' \
    -n 'tick-10sec { exit(0); }'
```

After 10 seconds, dtrace prints a table:

```text
 myfirst data           48512
 myfirst room           48317
 ...
```

The numbers should be roughly equal for `data_cv` and `room_cv` if the workload is symmetric (equal reads and writes). A large imbalance suggests one side is sleeping more than the other, which usually means a flow-control issue.

Another useful one-liner: histogram of cv_wait latency on the data cv:

```sh
# dtrace -n '
fbt::_cv_wait_sig:entry /args[0]->cv_description == "myfirst data"/ {
    self->ts = timestamp;
}
fbt::_cv_wait_sig:return /self->ts/ {
    @ = quantize(timestamp - self->ts);
    self->ts = 0;
}
tick-10sec { exit(0); }
'
```

The histogram shows the distribution of times threads spent inside `_cv_wait_sig`. Most should be short (signalled promptly). A long tail indicates threads sleeping for extended periods, which is normal for an idle device but suspicious for a busy one.

### Watching With vmstat and top

For a coarser view, `vmstat` and `top` running in the background provide context.

`vmstat 1` shows per-second statistics: CPU time spent in user, system, and idle; context switches; interrupts. During a stress run, `sy` (system time) should rise; `cs` (context switches) should rise as well, due to the cv signalling.

`top -SH` (the `-S` shows system processes; `-H` shows individual threads) shows per-thread CPU usage. During a stress run, the test threads should be visible. The `WCHAN` column shows what they are waiting on; expect to see the truncated cv descriptions (`myfirst ` for both `data_cv` and `room_cv`, since the trailing word is cut by `WMESGLEN`) plus, for any thread still using the Chapter 11 anonymous channel, the address of `&sc->cb` printed as a small numeric string.

Both are useful as background companions to a long stress run. They do not produce structured data, but they confirm at a glance that things are happening.

### Observing the Sysctls

A simple sanity check during the stress: read the sysctls periodically and verify they make sense.

```sh
$ while sleep 1; do
    sysctl dev.myfirst.0.stats.bytes_read \
           dev.myfirst.0.stats.bytes_written \
           dev.myfirst.0.stats.cb_used \
           dev.myfirst.0.debug_level \
           dev.myfirst.0.soft_byte_limit
  done
```

The byte counters should monotonically increase. The cb_used should hover in some range. The configuration should change as `config_writer` updates it.

If any sysctl read hangs (the `sysctl` command does not return), there is a synchronization problem with the sysctl handler. Probably a held mutex blocking the sysctl from acquiring the sx, or vice versa. Use `procstat -kk $$` from another terminal to see what the hanging shell is waiting on.

### Stress Test Acceptance Criteria

A driver passes a synchronization stress test if:

1. The composite script completes without panic.
2. `WITNESS` reports no warnings (`dmesg | grep -i witness | wc -l` returns zero).
3. Byte counts from readers and writers are within 1% of each other (small drift is acceptable due to timing of test stop).
4. `lockstat(1)` shows no lock with wait time exceeding 5% of total time.
5. The latency histogram from `lat_tester` shows the 99th percentile under one millisecond for an idle device, or under the configured timeout for a busy one.
6. Repeated runs (the long-duration loop) all pass.

These are not absolute thresholds; they are the values that have served the chapter's example. Real drivers may have stricter or looser bounds depending on their workload.

### Interpreting lockstat Output in Detail

`lockstat(1)` produces tables that look intimidating on first encounter. A short tour of the columns demystifies them.

A typical line for a contended lock:

```text
Adaptive mutex spin: 1234 events in 30.000 seconds (41.13 events/sec)

------------------------------------------------------------------------
   Count   nsec     ----- Lock -----                       Hottest Caller
   1234     321     myfirst0                              myfirst_read+0x91
```

What the columns mean:

- `Count`: number of events of this kind (acquires, in this case).
- `nsec`: average duration of the event (here, average time spinning before acquiring the lock).
- `Lock`: the lock's name.
- `Hottest Caller`: the function that most often experienced this event.

Lower in the output:

```text
Adaptive mutex block: 47 events in 30.000 seconds (1.57 events/sec)

------------------------------------------------------------------------
   Count   nsec     ----- Lock -----                       Hottest Caller
     47   58432     myfirst0                              myfirst_read+0x91
```

The "block" event is when the spin failed and the thread had to actually sleep. Average sleep time was 58 microseconds. That is high; it means a writer was holding the mutex during what should have been a short critical section.

Taken together, the spin events (1234) and block events (47) tell us the lock was contended 1281 times in 30 seconds, and 96% of the time the spin succeeded. That is a healthy pattern: most contention is brief, and only the rare long hold causes a real sleep.

For sleep locks (sx, cv), the columns are similar but the events are categorized differently:

```text
SX shared block: 2014 events in 30.000 seconds (67.13 events/sec)

------------------------------------------------------------------------
   Count   nsec     ----- Lock -----                       Hottest Caller
   2014    2105     myfirst cfg                            myfirst_get_debug_level+0x12
```

This says: shared waiters on the cfg sx blocked 2014 times, average wait 2.1 microseconds, mostly from the debug-level helper. With a config writer running, that is expected. Without the writer, it should be near zero.

The key skill in reading `lockstat` output is calibration: knowing what numbers are expected for your workload. A driver that has never been measured under load is a driver whose expected numbers are unknown. Run `lockstat` once with a known workload and save the output as the baseline. Future runs are then compared to the baseline; significant deviations are signal.

### Tracing Specific Code Paths With dtrace

Beyond the cv-counting and sleep-latency examples earlier, a few more `dtrace` recipes are useful for a chapter-12-style driver.

**Count cv waits per cv per second:**

```sh
# dtrace -n '
fbt::_cv_wait_sig:entry { @[args[0]->cv_description] = count(); }
tick-1sec { printa(@); trunc(@); }'
```

Prints a per-second tally of cv waits, broken down by cv name. Useful for spotting bursts.

**Trace which thread acquires the cfg sx exclusively:**

```sh
# dtrace -n '
fbt::_sx_xlock:entry /args[0]->lock_object.lo_name == "myfirst cfg"/ {
    printf("%s pid %d acquires cfg xlock\n", execname, pid);
    stack();
}'
```

Useful for confirming that the only writers are the sysctl handlers, not some other unexpected path.

**Histogram of myfirst_read latency:**

```sh
# dtrace -n '
fbt::myfirst_read:entry { self->ts = timestamp; }
fbt::myfirst_read:return /self->ts/ {
    @ = quantize(timestamp - self->ts);
    self->ts = 0;
}
tick-30sec { exit(0); }'
```

Same pattern as the cv-wait latency histogram, but at the handler level. Includes the time spent inside `cv_wait_sig` plus the time inside the cbuf operations and the uiomove.

These recipes are starting points. The `dtrace` provider for kernel functions (`fbt`) gives access to every function entry and return; the language is rich enough to express almost any aggregation.

### Wrapping Up Section 7

Realistic stress testing exercises the whole driver, not just one path. A composite workload that combines I/O, sysctl reads, and sysctl writes catches lock-ordering bugs that pure I/O testing would miss. `lockstat(1)` and `dtrace(1)` give you observability into the lock and cv activity without modifying the driver. A driver that passes the composite stress kit on a `WITNESS` kernel for an hour is a driver you can promote to the next chapter with confidence.

Section 8 closes the chapter with the housekeeping work: the documentation pass, the version bump, the regression test, and the changelog entry that tells your future self what you did and why.



## Section 8: Refactoring and Versioning Your Synchronized Driver

The driver now uses three primitives (`mtx`, `cv`, `sx`), has two lock classes with a documented order, supports interruptible and timed reads, and has a small configuration subsystem. The remaining work is the housekeeping pass: cleaning up the source for clarity, updating the documentation, bumping the version, running static analysis, and validating the regression test passes.

This section covers each. None of it is glamorous. All of it is what separates a working driver from a maintainable one.

### Cleaning Up the Source

After a chapter of focused changes, the source has accumulated some inconsistencies that are worth tidying.

**Group related code.** Move all the cv-related helpers next to each other (the wait helpers, the signal calls, the cv_init/cv_destroy in attach/detach). Move all the sx-related helpers together. The compiler does not care about ordering, but a reader does.

**Standardise the macro vocabulary.** Chapter 11 introduced `MYFIRST_LOCK`, `MYFIRST_UNLOCK`, `MYFIRST_ASSERT`. Add the symmetric set for the sx:

```c
#define MYFIRST_CFG_SLOCK(sc)   sx_slock(&(sc)->cfg_sx)
#define MYFIRST_CFG_SUNLOCK(sc) sx_sunlock(&(sc)->cfg_sx)
#define MYFIRST_CFG_XLOCK(sc)   sx_xlock(&(sc)->cfg_sx)
#define MYFIRST_CFG_XUNLOCK(sc) sx_xunlock(&(sc)->cfg_sx)
#define MYFIRST_CFG_ASSERT_X(sc) sx_assert(&(sc)->cfg_sx, SA_XLOCKED)
#define MYFIRST_CFG_ASSERT_S(sc) sx_assert(&(sc)->cfg_sx, SA_SLOCKED)
```

Now every lock acquisition in the driver goes through a macro. If we later switch from `sx` to `rw`, the change is in one header, not scattered across the source.

**Eliminate dead code.** If a helper from Chapter 11 is no longer called (perhaps the old wakeup channel is gone), remove it. Dead code attracts confusion.

**Comment the non-obvious bits.** Every lock acquisition that follows the lock-order rule deserves a one-line comment. Every place where snapshot-and-apply is used deserves a comment explaining why. The locking is the most subtle part of the driver; the comments should reflect that.

### Updating LOCKING.md

The Chapter 11 `LOCKING.md` documented one lock and a small set of fields. The Chapter 12 driver has more to say. The new version:

```markdown
# myfirst Locking Strategy

Version 0.6-sync (Chapter 12).

## Overview

The driver uses three synchronization primitives: a sleep mutex
(sc->mtx) for the data path, an sx lock (sc->cfg_sx) for the
configuration subsystem, and two condition variables (sc->data_cv,
sc->room_cv) for blocking reads and writes. Byte counters use
counter(9) per-CPU counters and protect themselves.

## Locks Owned by This Driver

### sc->mtx (mutex(9), MTX_DEF)

Protects:
- sc->cb (the circular buffer's internal state)
- sc->open_count, sc->active_fhs
- sc->is_attached (writes; reads at handler entry may be unprotected
  as an optimization, re-checked after every sleep)

### Lock-Free Plain Integers

- sc->read_timeout_ms, sc->write_timeout_ms: plain ints, accessed
  without locking. Safe because aligned int reads and writes are
  atomic on every architecture FreeBSD supports, and the values are
  advisory; a stale read just produces a slightly different timeout
  for the next wait. The sysctl framework writes them directly via
  CTLFLAG_RW.

### sc->cfg_sx (sx(9))

Protects:
- sc->cfg.debug_level
- sc->cfg.soft_byte_limit
- sc->cfg.nickname

Shared mode: every read of any cfg field.
Exclusive mode: every write of any cfg field.

### sc->data_cv (cv(9))

Wait condition: data is available in the cbuf.
Interlock: sc->mtx.
Signalled by: myfirst_write after a successful cbuf write.
Broadcast by: myfirst_detach.
Waiters: myfirst_read in myfirst_wait_data.

### sc->room_cv (cv(9))

Wait condition: room is available in the cbuf.
Interlock: sc->mtx.
Signalled by: myfirst_read after a successful cbuf read, and
myfirst_sysctl_reset after resetting the cbuf.
Broadcast by: myfirst_detach.
Waiters: myfirst_write in myfirst_wait_room.

## Lock-Free Fields

- sc->bytes_read, sc->bytes_written: counter_u64_t. Updates via
  counter_u64_add; reads via counter_u64_fetch.

## Lock Order

sc->mtx -> sc->cfg_sx

A thread holding sc->mtx may acquire sc->cfg_sx in either mode.
A thread holding sc->cfg_sx may NOT acquire sc->mtx.

Rationale: the data path always holds sc->mtx and may need to read
configuration during its critical section. The configuration path
(sysctl writers) does not need the data mutex; if a future feature
requires both, it must acquire sc->mtx first.

## Locking Discipline

1. Acquire mutex with MYFIRST_LOCK(sc), release with MYFIRST_UNLOCK(sc).
2. Acquire sx in shared mode with MYFIRST_CFG_SLOCK, exclusive with
   MYFIRST_CFG_XLOCK. Release with the matching unlock.
3. Wait on a cv with cv_wait_sig (interruptible) or
   cv_timedwait_sig (interruptible + bounded).
4. Signal a cv with cv_signal (one waiter) or cv_broadcast (all
   waiters). Use cv_broadcast only for state changes that affect all
   waiters (detach, configuration reset).
5. NEVER hold sc->mtx across uiomove(9), copyin(9), copyout(9),
   selwakeup(9), or wakeup(9). Each of these may sleep or take
   other locks. cv_wait_sig is the exception (it atomically drops
   the interlock).
6. NEVER hold sc->cfg_sx across uiomove(9) etc., for the same
   reason.
7. All cbuf_* calls must happen with sc->mtx held (the helpers
   assert MA_OWNED).
8. The detach path clears sc->is_attached under sc->mtx, broadcasts
   both cvs, and refuses detach while active_fhs > 0.

## Snapshot-and-Apply Pattern

When a path needs both sc->mtx and sc->cfg_sx, it should follow
the snapshot-and-apply pattern:

  1. sx_slock(&sc->cfg_sx); read cfg into local variables;
     sx_sunlock(&sc->cfg_sx).
  2. MYFIRST_LOCK(sc); do cbuf operations using the snapshot;
     MYFIRST_UNLOCK(sc).

The snapshot may be slightly stale by the time it is used. For
configuration values that are advisory (debug level, soft byte
limit), this is acceptable.

## Known Non-Locked Accesses

### sc->is_attached at handler entry

Unprotected plain read. Safe because:
- A stale "true" is re-checked after every sleep via
  if (!sc->is_attached) return (ENXIO).
- A stale "false" causes the handler to return ENXIO early, which
  is also what it would do with a fresh false.

### sc->open_count, sc->active_fhs at sysctl read time

Unprotected plain loads. Safe on amd64 and arm64 (aligned 64-bit
loads are atomic). Acceptable on i386 because the torn read, if
it ever happened, would produce a single bad statistic with no
correctness impact.

## Wait Channels

- sc->data_cv: data has become available.
- sc->room_cv: room has become available.

(The legacy &sc->cb wakeup channel from Chapter 10 has been
retired in Chapter 12.)

## History

- 0.6-sync (Chapter 12): added cv channels, sx for configuration,
  bounded reads via cv_timedwait_sig.
- 0.5-kasserts (Chapter 11, Stage 5): KASSERT calls added
  throughout cbuf helpers and wait helpers.
- 0.5-counter9 (Chapter 11, Stage 3): byte counters migrated to
  counter(9).
- 0.5-concurrency (Chapter 11, Stage 2): MYFIRST_LOCK/UNLOCK/ASSERT
  macros, explicit locking strategy.
- Earlier versions: see Chapter 10 / Chapter 11 history.
```

That document is now the authoritative description of the driver's synchronization story. Any future change updates the document in the same commit as the code change. A reviewer who wants to know whether a change is safe reads the diff against the document, not against the code.

### Bumping the Version

Update the version string:

```c
#define MYFIRST_VERSION "0.6-sync"
```

Print it at attach (the existing `device_printf` line in attach already includes the version):

```c
device_printf(dev,
    "Attached; version %s, node /dev/%s (alias /dev/myfirst), "
    "cbuf=%zu bytes\n",
    MYFIRST_VERSION, devtoname(sc->cdev), cbuf_size(&sc->cb));
```

Update the changelog:

```markdown
## 0.6-sync (Chapter 12)

- Replaced anonymous wakeup channel (&sc->cb) with two named
  condition variables (sc->data_cv, sc->room_cv).
- Added bounded read support via sc->read_timeout_ms sysctl,
  using cv_timedwait_sig under the hood.
- Added a small configuration subsystem (sc->cfg) protected by
  an sx lock (sc->cfg_sx).
- Added sysctl handlers for debug_level, soft_byte_limit, and
  nickname.
- Added myfirst_sysctl_reset that takes both locks in the canonical
  order to clear the cbuf and reset counters.
- Updated LOCKING.md with the new primitives, the lock order, and
  the snapshot-and-apply pattern.
- Added MYFIRST_CFG_* macros symmetric with the existing MYFIRST_*
  mutex macros.
- All Chapter 11 tests continue to pass; new sysctl-based tests
  added under userland/.
```

### Updating the README

The Chapter 11 README named the driver and described its features. The Chapter 12 README adds the new ones:

```markdown
# myfirst

A FreeBSD 14.3 pseudo-device driver that demonstrates buffered I/O,
concurrency, and modern synchronization primitives. Developed as the
running example for the book "FreeBSD Device Drivers: From First
Steps to Kernel Mastery."

## Status

Version 0.6-sync (Chapter 12).

## Features

- A Newbus pseudo-device under nexus0.
- A primary device node at /dev/myfirst/0 (alias: /dev/myfirst).
- A circular buffer (cbuf) as the I/O buffer.
- Blocking, non-blocking, and timed reads and writes.
- poll(2) support via d_poll and selinfo.
- Per-CPU byte counters via counter(9).
- A single sleep mutex protects composite cbuf state; see LOCKING.md.
- Two named condition variables (data_cv, room_cv) coordinate read
  and write blocking.
- An sx lock protects the runtime configuration (debug_level,
  soft_byte_limit, nickname).

## Configuration

Three runtime-tunable parameters via sysctl:

- dev.myfirst.<unit>.debug_level (0-3): controls dmesg verbosity.
- dev.myfirst.<unit>.soft_byte_limit: refuse writes that would
  push cb_used above this threshold (0 = no limit).
- dev.myfirst.<unit>.nickname: a string used in log messages.
- dev.myfirst.<unit>.read_timeout_ms: bound a blocking read.

(The last is per-instance; see myfirst.4 for details, when written.)

## Build and Load

    $ make
    # kldload ./myfirst.ko
    # dmesg | tail
    # ls -l /dev/myfirst
    # printf 'hello' > /dev/myfirst
    # cat /dev/myfirst
    # kldunload myfirst

## Tests

See ../../userland/ for the test programs. The Chapter 12 tests
include config_writer (toggles sysctls during stress) and
timeout_tester (verifies bounded reads).

## License

BSD 2-Clause. See individual source files for SPDX headers.
```

### Running Static Analysis

Run `clang --analyze` against the Chapter 12 driver:

```sh
$ make WARNS=6 clean all
$ clang --analyze -D_KERNEL -I/usr/src/sys \
    -I/usr/src/sys/amd64/conf/GENERIC myfirst.c
```

Triage the output. New warnings since Chapter 11 should be either:

1. False positives (clang does not understand the locking discipline). Document each.
2. Real bugs. Fix each.

Common false positives in driver code involve `sx_assert` and `mtx_assert` macros that clang cannot see through; the analyser thinks the lock might not be held even when the assert proves it is. These are acceptable to silence with `__assert_unreachable()` or by restructuring the code to make the lock state more obvious to the analyser.

### Running the Regression Suite

The Chapter 11 regression script extends naturally:

```sh
#!/bin/sh
# regression.sh: full Chapter 12 regression.

set -eu

die() { echo "FAIL: $*" >&2; exit 1; }
ok()  { echo "PASS: $*"; }

[ $(id -u) -eq 0 ] || die "must run as root"
kldstat | grep -q myfirst && kldunload myfirst
[ -f ./myfirst.ko ] || die "myfirst.ko not built; run make first"

kldload ./myfirst.ko
trap 'kldunload myfirst 2>/dev/null || true' EXIT

sleep 1
[ -c /dev/myfirst ] || die "device node not created"
ok "load"

# Chapter 7-10 tests.
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

# Chapter 12-specific tests.
../userland/timeout_tester || die "timeout_tester failed"
ok "timeout_tester"

../userland/config_writer 5 &
CW=$!
../userland/mt_reader || die "mt_reader (under config writer) failed"
wait $CW
ok "mt_reader under config writer"

sysctl dev.myfirst.0.stats >/dev/null || die "sysctl stats not accessible"
sysctl dev.myfirst.0.debug_level >/dev/null || die "sysctl debug_level not accessible"
sysctl dev.myfirst.0.soft_byte_limit >/dev/null || die "sysctl soft_byte_limit not accessible"
ok "sysctl"

# WITNESS check.
WITNESS_HITS=$(dmesg | grep -ci "witness\|lor" || true)
if [ "$WITNESS_HITS" -gt 0 ]; then
    die "WITNESS warnings detected ($WITNESS_HITS lines)"
fi
ok "witness clean"

echo "ALL TESTS PASSED"
```

A green run after each commit is the minimum bar. A green run on a `WITNESS` kernel after a long-duration composite is the higher bar.

### Pre-Commit Checklist

The Chapter 11 checklist gets two new items for Chapter 12:

1. Have I updated `LOCKING.md` with any new locks, cvs, or order changes?
2. Have I run the full regression suite on a `WITNESS` kernel?
3. Have I run the long-duration composite stress for at least 30 minutes?
4. Have I run `clang --analyze` and triaged every new warning?
5. Have I added a `sx_assert` or `mtx_assert` for any new helper that expects a lock state?
6. Have I bumped the version string and updated `CHANGELOG.md`?
7. Have I verified that the test kit builds and runs?
8. **(New)** Have I checked that every cv has both a signaller and a documented condition?
9. **(New)** Have I checked that every sx_xlock has a paired sx_xunlock on every code path, including error paths?

The two new items capture the most common bugs in Chapter 12-style code. A cv without a signaller is dead weight (waiters will never wake). An sx_xlock without a paired unlock on an error path is a quiet deadlock waiting to happen.

### Wrapping Up Section 8

The driver is now not only correct but verifiably correct, well-documented, and versioned. It has:

- An updated `LOCKING.md` describing three primitives, two lock classes, and one canonical lock order.
- A new version string (0.6-sync) reflecting the Chapter 12 work.
- A regression script that exercises every primitive and validates `WITNESS` cleanliness.
- A pre-commit checklist that catches the two new failure modes Chapter 12 introduced.

That is the close of the chapter's main teaching arc. The labs and challenges follow.



## Hands-on Labs

These labs consolidate the Chapter 12 concepts through direct hands-on experience. They are ordered from least to most demanding. Each is designed to be completable in a single lab session.

### Pre-Lab Setup Checklist

Before starting any lab, confirm the four items below. The Chapter 11 checklist applies; we add three Chapter 12-specific ones.

1. **Debug kernel running.** `sysctl kern.ident` reports the kernel with `INVARIANTS` and `WITNESS`.
2. **WITNESS active.** `sysctl debug.witness.watch` returns a non-zero value.
3. **Driver source matches Chapter 11 Stage 5 (kasserts).** From your driver directory, `make clean && make` should compile cleanly.
4. **A clean dmesg.** `dmesg -c >/dev/null` once before the first lab.
5. **(New)** **Companion userland built.** From `examples/part-03/ch12-synchronization-mechanisms/userland/`, `make` should produce the `config_writer` and `timeout_tester` binaries.
6. **(New)** **Chapter 11 stress kit available.** The labs reuse `mp_stress`, `mt_reader`, and `producer_consumer` from Chapter 11.
7. **(New)** **Backup of Stage 5.** Copy the working Stage 5 driver to a safe location before starting any lab that modifies the source. Several labs intentionally introduce bugs that need to be reverted cleanly.

### Lab 12.1: Replace Anonymous Wakeup Channels With Condition Variables

**Goal.** Convert the Chapter 11 driver from `mtx_sleep`/`wakeup` on the anonymous channel `&sc->cb` to two named condition variables (`data_cv` and `room_cv`).

**Steps.**

1. Copy your Stage 5 driver into `examples/part-03/ch12-synchronization-mechanisms/stage1-cv-channels/`.
2. Add `struct cv data_cv` and `struct cv room_cv` to `struct myfirst_softc`.
3. In `myfirst_attach`, call `cv_init(&sc->data_cv, "myfirst data")` and `cv_init(&sc->room_cv, "myfirst room")`. Place them after the mutex init.
4. In `myfirst_detach`, before `mtx_destroy`, call `cv_broadcast` on each cv to wake any sleepers, then `cv_destroy` on each.
5. Replace the `mtx_sleep(&sc->cb, ...)` calls in `myfirst_wait_data` and `myfirst_wait_room` with `cv_wait_sig(&sc->data_cv, &sc->mtx)` and `cv_wait_sig(&sc->room_cv, &sc->mtx)` respectively.
6. Replace the `wakeup(&sc->cb)` calls in `myfirst_read` and `myfirst_write` with `cv_signal(&sc->room_cv)` and `cv_signal(&sc->data_cv)` respectively. Note the swap: a successful read frees room (so wake writers); a successful write produces data (so wake readers).
7. Build, load, run the Chapter 11 stress kit.

**Verification.** All Chapter 11 tests pass. `procstat -kk` against a sleeping reader shows wait channel `myfirst ` (the truncated form of `"myfirst data"`; see the Section 2 note about `WMESGLEN`). No `WITNESS` warnings.

**Stretch goal.** Use `dtrace` to count signals to each cv during `mp_stress`. Confirm that the signal counts are roughly equal between data_cv and room_cv (because reads and writes are roughly equal).

### Lab 12.2: Add Bounded Reads

**Goal.** Add a `read_timeout_ms` sysctl that bounds blocking reads.

**Steps.**

1. Copy Lab 12.1 into `stage2-bounded-read/`.
2. Add an `int read_timeout_ms` field to the softc. Initialize to 0 in attach.
3. Register a `SYSCTL_ADD_INT` for it under `dev.myfirst.<unit>.read_timeout_ms`, with `CTLFLAG_RW`.
4. Modify `myfirst_wait_data` to use `cv_timedwait_sig` when `read_timeout_ms > 0`, converting milliseconds to ticks. Translate `EWOULDBLOCK` to `EAGAIN`.
5. Build and load.
6. Build the `timeout_tester` from `examples/part-03/ch12-synchronization-mechanisms/userland/`.
7. Set the sysctl to 100, run `timeout_tester`, observe that `read(2)` returns `EAGAIN` after about 100 ms.
8. Reset the sysctl to 0, run `timeout_tester` again. The read blocks until you Ctrl-C, returning `EINTR`.

**Verification.** The output of `timeout_tester` matches expectations for both timeout and signal-interruption cases. Stress kit still passes.

**Stretch goal.** Add a symmetric `write_timeout_ms` sysctl and verify it bounds writes when the buffer is full.

### Lab 12.3: Add an sx-Protected Configuration Subsystem

**Goal.** Add the `cfg` struct and `cfg_sx` lock from Section 5; expose `debug_level` as a sysctl.

**Steps.**

1. Copy Lab 12.2 into `stage3-sx-config/`.
2. Add `struct sx cfg_sx` and `struct myfirst_config cfg` to the softc. Initialize in attach (`sx_init(&sc->cfg_sx, "myfirst cfg")`; default values for cfg fields). Destroy in detach.
3. Add a `myfirst_sysctl_debug_level` handler following the snapshot-and-apply pattern. Register it.
4. Add a `MYFIRST_DBG(sc, level, fmt, ...)` macro that consults `sc->cfg.debug_level` via `sx_slock`.
5. Sprinkle a few `MYFIRST_DBG(sc, 1, ...)` calls in the read/write paths to log when the buffer becomes empty or full.
6. Build and load.
7. Run `mp_stress`. Confirm no log spam (debug_level defaults to 0).
8. `sysctl -w dev.myfirst.0.debug_level=2` and run `mp_stress` again. Now `dmesg` should show debug messages.
9. Reset the level to 0.

**Verification.** Debug messages appear and disappear as the sysctl changes. No `WITNESS` warnings during the toggle.

**Stretch goal.** Add the `soft_byte_limit` sysctl. Set it to 1024 and run a writer that produces bursts of 4096 bytes; confirm that the writer sees `EAGAIN` early.

### Lab 12.4: Inspect Held Locks With DDB

**Goal.** Use the in-kernel debugger to inspect a hung test.

**Steps.**

1. Make sure the debug kernel has `options DDB` and a configured way to drop into DDB (typically `Ctrl-Alt-Esc` on a serial console, or the `Break` key).
2. Load Lab 12.3's driver.
3. Start a `cat /dev/myfirst` in one terminal. It blocks (no producer).
4. From the console (or via `sysctl debug.kdb.enter=1`), drop into DDB.
5. Run `show all locks`. Note any threads holding locks.
6. Run `ps`. Find the `cat` process and the `myfirst data` wait channel.
7. Run `bt <pid> <tid>` for the cat thread. Confirm the backtrace ends in `_cv_wait_sig`.
8. `continue` to leave DDB.
9. Send `SIGINT` to the cat (Ctrl-C).

**Verification.** The cat returns with `EINTR`. No panics. You have a transcript of the DDB session.

**Stretch goal.** Repeat with `mp_stress` running concurrently. Compare `show all locks` output: more locks, more activity, but the same shape.

### Lab 12.5: Detect a Deliberate Lock Order Reversal

**Goal.** Introduce a deliberate LOR and observe `WITNESS` catching it.

**Steps.**

1. Copy Lab 12.3 into a scratch directory `stage-lab12-5/`. Do not modify Lab 12.3 in place.
2. Add a path that violates the lock order. For example, in a small experimental sysctl handler:

   ```c
   /* WRONG: sx first, then mtx, reversing the canonical order. */
   sx_xlock(&sc->cfg_sx);
   MYFIRST_LOCK(sc);
   /* trivial work */
   MYFIRST_UNLOCK(sc);
   sx_xunlock(&sc->cfg_sx);
   ```

3. Build and load on the `WITNESS` kernel.
4. Run `mp_stress` (which exercises the canonical order via the data path) and trigger the new sysctl simultaneously.
5. Watch `dmesg` for the `lock order reversal` warning.
6. Record the warning text. Note the line numbers.
7. Delete the scratch directory; do not commit the bug.

**Verification.** `dmesg` shows a `lock order reversal` warning naming both locks and both source locations.

**Stretch goal.** Determine, just from the `WITNESS` output, where the canonical order was first established. Open the source at that line and confirm.

### Lab 12.6: Long-Running Composite Stress

**Goal.** Run the composite stress workload from Section 7 for 30 minutes and verify clean.

**Steps.**

1. Boot the debug kernel.
2. Build and load `examples/part-03/ch12-synchronization-mechanisms/stage4-final/`. This is the final integrated driver (cv channels + bounded reads + sx-protected configuration + the reset sysctl). All the Section 7 sysctls that the composite script touches are present here.
3. Build the userland testers.
4. Save the Section 7 composite stress script as `composite_stress.sh`.
5. Wrap it in a 30-minute loop:
   ```sh
   for i in $(seq 1 30); do
     ./composite_stress.sh
     echo "iteration $i done"
   done
   ```
6. Monitor `dmesg` periodically.
7. After completion, check:
   - `dmesg | grep -ci witness` returns 0.
   - All loop iterations completed.
   - `vmstat -m | grep cbuf` shows the expected static allocation (no growth).

**Verification.** All criteria met. The driver survives 30 minutes of composite stress on a debug kernel without warning, panic, or memory growth.

**Stretch goal.** Run the same loop for 24 hours on a dedicated test machine. Bugs that appear at this scale are the ones that cost the most in production.

### Lab 12.7: Verify the Snapshot-and-Apply Pattern Holds Under Contention

**Goal.** Show that the snapshot-and-apply pattern in `myfirst_write` correctly handles concurrent updates to the soft byte limit.

**Steps.**

1. Set the soft byte limit to a small value: `sysctl -w dev.myfirst.0.soft_byte_limit=512`.
2. Start `mp_stress` with two writers and two readers.
3. From a third terminal, repeatedly toggle the limit: `while sleep 0.1; do sysctl -w dev.myfirst.0.soft_byte_limit=$RANDOM; done`.
4. Watch the writer output. Some writes will succeed; others will return `EAGAIN` (the limit was below the current cb_used at the moment of check).
5. Watch `dmesg` for `WITNESS` warnings.

**Verification.** No `WITNESS` warnings. The byte counts in `mp_stress` are slightly lower than usual (because some writes were refused), but the total written approximately equals the total read.

**Stretch goal.** Modify `myfirst_write` to violate the lock-order rule by acquiring the cfg sx while holding the data mutex. Reload, run the same test. `WITNESS` should fire on the first run that exercises both paths simultaneously. Revert the change.

### Lab 12.8: Profile With lockstat

**Goal.** Use `lockstat(1)` to characterize the contended locks under stress.

**Steps.**

1. Load Lab 12.3's driver on the debug kernel.
2. Start `mp_stress` in one terminal.
3. From another, run `lockstat -P sleep 30 > /tmp/lockstat.out`.
4. Open the output file. Find the entries for `myfirst0` (mtx) and `myfirst cfg` (sx).
5. Note: the maximum hold time, the average hold time, the maximum wait time, the average wait time, and the acquisition count.
6. Repeat with `config_writer` running. Compare the `myfirst cfg` numbers.

**Verification.** The numbers match the expected profile. The mutex shows millions of acquisitions with short hold times. The sx shows tens of thousands of acquisitions, mostly shared, with very short hold times.

**Stretch goal.** Modify the driver to artificially extend a critical section (e.g., add a 10 ms `pause(9)` inside the mutex). Re-run `lockstat`. Observe the contention spike. Revert the modification.



## Challenge Exercises

The challenges extend Chapter 12 beyond the baseline labs. Each is optional; each is designed to deepen your understanding.

### Challenge 1: Use sx_downgrade for a Config Refresh

The `myfirst_sysctl_debug_level` handler currently drops the shared lock and reacquires the exclusive lock. An alternative is to acquire shared, attempt `sx_try_upgrade`, and `sx_downgrade` after the modification. Implement this variant. Compare the behaviour under contention. When does each pattern win?

### Challenge 2: Implement a Drain Operation Using cv_broadcast

Add an ioctl or sysctl that "drains" the cbuf: blocks until `cb_used == 0`, then returns. The implementation should use `cv_wait_sig(&sc->room_cv, ...)` in a loop on the condition `cb_used > 0`. Verify that `cv_broadcast(&sc->room_cv)` after the drain wakes every waiter, not just one.

### Challenge 3: A dtrace Script for cv_wait Latency

Write a `dtrace` script that produces a histogram of how long threads spend inside `cv_wait_sig` on each of `data_cv` and `room_cv`. Run it during `mp_stress`. What does the distribution look like? Where is the long tail?

### Challenge 4: Replace cv With Anonymous Channels

Re-implement the data and room conditions using `mtx_sleep` and `wakeup` on anonymous channels (a regression to the Chapter 11 design). Run the tests. The driver should still work, but the `procstat -kk` output and the `dtrace` queries become less informative. Describe the readability difference.

### Challenge 5: Add Per-Descriptor read_timeout_ms

The `read_timeout_ms` sysctl is per-device. Add a per-descriptor timeout via an `ioctl(2)`: `MYFIRST_SET_READ_TIMEOUT(int ms)` on a file descriptor sets that descriptor's timeout. The driver code becomes more interesting because the timeout now lives in `struct myfirst_fh` rather than `struct myfirst_softc`. Beware: the per-fh state is not shared with other descriptors (no lock needed for the field itself), but the choice of timeout still affects the wait helper.

### Challenge 6: Use rw(9) Instead of sx(9)

Replace `sx_init` with `rw_init`, `sx_xlock` with `rw_wlock`, and so on. Run the tests. What breaks? (Hint: the cfg path may include a sleeping operation; rw is not sleepable.) What does the failure look like? When would `rw(9)` be the right choice?

### Challenge 7: Implement a Multi-CV Drain

The driver has two cvs. Suppose detach should be considered complete only when both cvs have zero waiters. Implement a check in detach that loops until `data_cv.cv_waiters == 0` and `room_cv.cv_waiters == 0`, sleeping briefly between checks. (Note: accessing `cv_waiters` directly from outside the cv API is non-portable; this is an exercise in understanding the internal state. Real production code should use a different mechanism.)

### Challenge 8: Lock-Order Visualization

Use `dtrace` or `lockstat` to produce a graph of lock acquisitions during `mp_stress`. The nodes are locks; the edges are "holder of A acquired B while still holding A". Compare the graph to your `LOCKING.md` lock order. Are there acquisitions you did not anticipate?

### Challenge 9: Sleep-Channel Comparison

Build two versions of the driver: one using cv (the Chapter 12 default) and one using legacy `mtx_sleep`/`wakeup` on anonymous channels (the Chapter 11 default). Run identical workloads on both. Measure: maximum throughput, latency at the 99th percentile, `WITNESS` cleanliness, and the readability of the source. Write a one-page report.

### Challenge 10: Bound Configuration Writes

The Chapter 12 driver allows configuration writes at any time. Add a sysctl `cfg_write_cooldown_ms` that limits how often a configuration change can occur (e.g., at most one write per 100 ms). Implement this with a timestamp field in the cfg struct and a check in each cfg sysctl handler. Decide what to do when the cooldown is violated: return `EBUSY`, queue the change, or silently coalesce. Document the choice.



## Troubleshooting

This reference catalogues the bugs you are most likely to encounter while working through Chapter 12.

### Symptom: Reader hangs forever despite data being written

**Cause.** A lost wakeup. The writer added bytes but did not signal `data_cv`, or the signal targeted the wrong cv.

**Fix.** Search the source for every place that adds bytes; ensure `cv_signal(&sc->data_cv)` is called. Confirm the cv is the one waiters are blocked on.

### Symptom: WITNESS warns "lock order reversal" between sc->mtx and sc->cfg_sx

**Cause.** A path took the locks in the wrong order. The canonical order is mtx first, sx second.

**Fix.** Find the offending path (the warning names the lines). Either reorder the acquisitions to match the canonical order, or refactor the path to avoid holding both locks simultaneously (snapshot-and-apply).

### Symptom: cv_timedwait_sig returns EWOULDBLOCK immediately

**Cause.** The timeout in ticks was zero or negative. Most likely the conversion from milliseconds to ticks rounded down to zero.

**Fix.** Use the `(timo_ms * hz + 999) / 1000` formula to round up to at least one tick. Verify `hz` is the expected value (typically 1000 on FreeBSD 14.3).

### Symptom: detach hangs

**Cause.** A thread is sleeping on a cv that has not been broadcast, or the detach is waiting for `active_fhs > 0` to drop and a descriptor is open.

**Fix.** Confirm that detach broadcasts both cvs before the active_fhs check. Use `fstat | grep myfirst` from a separate terminal to find any process holding the device open; kill it.

### Symptom: sysctl write hangs

**Cause.** The sysctl handler is waiting for a lock that is held by a thread doing something blocking. Most commonly, the cfg sx is held in exclusive mode by a slow `sysctl_handle_string`.

**Fix.** Verify the sysctl handler follows the snapshot-and-apply pattern: acquire shared, read, release; then `sysctl_handle_*` outside the lock; then exclusive lock to commit. Holding the lock across `sysctl_handle_*` is the bug.

### Symptom: sx_destroy panics with "lock still held"

**Cause.** `sx_destroy` was called while another thread still held the lock or was waiting for it.

**Fix.** Confirm that detach refuses to proceed while `active_fhs > 0`. Confirm that no kernel thread or callout is using the cfg sx after detach starts.

### Symptom: cv_signal or cv_broadcast wakes nothing visible

**Cause.** No one was waiting on the cv at the moment of the signal. Both `cv_signal` and `cv_broadcast` are no-ops when the wait queue is empty, and a `dtrace` probe on the wake side sees no follow-on activity.

**Fix.** None needed; the empty wake is correct and harmless. If you expected a waiter and there was none, the bug is upstream: either the waiter never reached `cv_wait_sig`, or the waker is targeting the wrong cv. Confirm via `dtrace` that the signal is firing on the cv you intend, and `procstat -kk` against the waiter to confirm where it is sleeping.

### Symptom: read_timeout_ms set to 100 produces 200 ms latency

**Cause.** The kernel's `hz` value is lower than expected. The `+999` round-up means a 100 ms timeout at `hz=100` becomes 10 ticks (100 ms), but if `hz=10` it becomes 1 tick (100 ms). Different rounding.

**Fix.** Confirm `hz` with `sysctl kern.clockrate`. For tighter timeouts, use `cv_timedwait_sig_sbt` directly with `SBT_1MS * timo_ms` to avoid the tick rounding.

### Symptom: A deliberately-broken lock order does not produce a WITNESS warning

**Cause.** Either the path with the bug is not exercised by the test, or `WITNESS` is not enabled in the running kernel.

**Fix.** Confirm `sysctl debug.witness.watch` returns nonzero. Confirm the offending path runs (add a `device_printf` to verify). Run the test under `mp_stress` to maximize the chance of the bug surfacing.

### Symptom: lockstat shows enormous wait times on the data mutex

**Cause.** The mutex is being held across a long operation. Common offenders: `uiomove` accidentally inside the critical section; a debug `device_printf` that prints a large string while holding the lock.

**Fix.** Audit the critical sections. Move long operations outside. The mutex should be held for tens of nanoseconds, not microseconds.

### Symptom: mp_stress reports byte-count mismatch after Chapter 12 changes

**Cause.** A wakeup was missed during the cv refactor. A reader started waiting after a writer's signal had already been delivered (no waiter at signal time, signal lost).

**Fix.** Verify that the wait helpers use `while`, not `if`, around `cv_wait_sig`. Verify that the signal happens after the state change, not before.

### Symptom: timeout_tester shows latency longer than the configured timeout

**Cause.** Scheduler latency. The kernel scheduled the thread some milliseconds after the timer fired. This is normal; expect a few ms of jitter.

**Fix.** None for typical workloads. For real-time workloads, raise the thread's priority via `rtprio(2)`.

### Symptom: kldunload reports busy when no descriptor is open

**Cause.** A taskqueue or background thread is still using a primitive in the driver. (Should not happen for our chapter, but worth knowing.)

**Fix.** Audit any taskqueue-, callout-, or kthread-spawning code. The detach must drain or terminate all of them before declaring it safe to unload.

### Symptom: cv_wait_sig wakes immediately and returns 0

**Cause.** A signal arrived while the wait was being set up, or the cv was signalled by a thread that ran just before the wait was issued. Not actually a bug; the `while` loop is supposed to handle it.

**Fix.** Confirm that the surrounding `while (!condition)` re-checks. The loop turns the spurious-looking wakeup into a no-op: re-check, find condition false, sleep again.

### Symptom: Two threads waiting on the same cv get woken in unexpected order

**Cause.** `cv_signal` wakes one waiter chosen by sleep-queue policy (highest priority, FIFO among equal). It does not wake them in arrival order if their priorities differ.

**Fix.** None usually needed; the kernel's choice is correct. If you require strict arrival-order wake-up, use a different design (per-waiter cv, or an explicit queue).

### Symptom: sx_xlock under heavy reader load takes seconds to acquire

**Cause.** Many shared holders, each releasing slowly because the cfg sx is being acquired and released on every I/O. The writer is starved by the constant trickle of readers.

**Fix.** The kernel uses the `SX_LOCK_WRITE_SPINNER` flag to give writers priority once they begin waiting; the starvation is bounded but can still produce visible latency. If the latency is unacceptable, redesign so writers happen during quiescent windows or under a different protocol.

### Symptom: Tests pass on a non-WITNESS kernel but fail on WITNESS

**Cause.** Almost always a real bug that `WITNESS` has detected. The most common: a lock acquired in a path that violates the global order, but the deadlock has not yet manifested because the contending workload has not been hit.

**Fix.** Read the `WITNESS` warning carefully. The warning text includes the source location of every violation. Fix the violation; the test should then pass on both kernels.

### Symptom: Locking macros expand to nothing on the non-debug kernel

**Cause.** This is by design. `mtx_assert`, `sx_assert`, `KASSERT`, and `MYFIRST_ASSERT(sc)` (which expands to `mtx_assert`) compile to nothing without `INVARIANTS`. The asserts are free at production time and informative at development time.

**Fix.** None needed. Confirm that your test kernel has `INVARIANTS` enabled and the asserts will fire when violated.

### Symptom: Sysctl handler blocks the entire system

**Cause.** A sysctl handler that holds a lock across a slow operation can effectively serialize every other operation that needs the same lock. If the lock is the device's main mutex, every I/O is blocked until the sysctl returns.

**Fix.** Sysctl handlers should follow the same discipline as I/O handlers: hold the lock for the minimum time, release before any potentially-slow operation. The snapshot-and-apply pattern works equally well here.

### Symptom: A reader gets EAGAIN even with read_timeout_ms=0

**Cause.** The read returned `EAGAIN` because of `O_NONBLOCK` (the file descriptor was opened non-blocking, or `fcntl(2)` set `O_NONBLOCK` on it). The driver's `IO_NDELAY` check returns `EAGAIN` regardless of the timeout sysctl.

**Fix.** Confirm the descriptor is blocking: `fcntl(fd, F_GETFL)` should return a value without the `O_NONBLOCK` bit set. If non-blocking is intended, `EAGAIN` is the correct response.

### Symptom: kldunload after a successful test still hangs briefly

**Cause.** The detach path is waiting for in-flight handlers to return. Each waiter that was sleeping on a cv must wake (because of the broadcast), reacquire the mutex, see `!is_attached`, return, and exit the kernel. This takes a few milliseconds for several waiters.

**Fix.** None usually needed; a few-millisecond delay is normal. If the delay is longer, check that every waiter does have a post-sleep `is_attached` check.

### Symptom: Two separate driver instances both report WITNESS warnings about the same lock name

**Cause.** Both instances initialize their locks with the same name (`myfirst0` for both, for example). `WITNESS` treats locks of the same name as the same logical lock and may warn about duplicate acquisition or invented order issues across instances.

**Fix.** Initialize each instance's lock with a unique name that includes the unit number, for example via `device_get_nameunit(dev)` which yields `myfirst0`, `myfirst1`, etc. Our chapter already does this for the device mutex; do the same for cvs and sx.

### Symptom: A cv with many waiters takes a long time to broadcast

**Cause.** `cv_broadcast` walks the wait queue, marking each waiter as runnable. The walk is O(n) in the number of waiters. With hundreds of waiters this becomes a measurable cost.

**Fix.** The broadcast itself is rarely a bottleneck for normal workloads; it is the subsequent thundering-herd contention as every awakened thread tries to acquire the interlock that causes visible pause. If your driver routinely has hundreds of waiters on the same cv, reconsider the design; per-waiter cvs or a queue-based approach may scale better.



## Wrapping Up

Chapter 12 took the driver you built in Chapter 11 and gave it a richer synchronization vocabulary. The single mutex from Chapter 11 is still there, doing the same job, with the same rules. Around it now sit two named condition variables that replace the anonymous wakeup channel, an sx lock that protects a small but real configuration subsystem, and a bounded-wait capability that lets the read path return promptly when the user expects it to. The lock order between the two lock classes is documented, enforced by `WITNESS`, and verified by a stress kit that runs the data path and the configuration path concurrently.

We learned to think about synchronization as a vocabulary, not just a mechanism. A condition variable says *I am waiting for a specific change; tell me when it happens*. A shared lock says *I am reading; do not let a writer in*. A timed wait says *and please give up if it takes too long*. Each primitive is a different shape of agreement between threads, and using the right shape for each agreement produces code that reads like the design rather than fights against it.

We also learned to debug synchronization carefully. The six failure shapes (lost wakeup, spurious wakeup, lock-order reversal, premature destroy, sleeping with a non-sleepable lock, race between detach and active operation) cover almost every bug you will hit in practice. `WITNESS` catches the ones the kernel can detect at runtime; the in-kernel debugger lets you inspect a hung system; `lockstat(1)` and `dtrace(1)` give you observability without modifying the source.

We finished with a refactor pass. The driver now has a documented lock order, a clean `LOCKING.md`, a bumped version string, an updated changelog, and a regression test that verifies every primitive on every supported workload. That infrastructure scales: when Chapter 13 adds timers and Chapter 14 adds interrupts, the documentation pattern absorbs the new primitives without growing brittle.

### What You Should Now Be Able to Do

A short self-checklist of capabilities you should now have, before turning to Chapter 13:

- Look at a piece of shared state in any driver and choose the right primitive (atomic, mutex, sx, rw, cv) by walking the decision tree.
- Replace any anonymous wakeup channel in any driver with a named cv, and explain why the change is an improvement.
- Add a bounded blocking primitive to any wait path, and explain when to use `EAGAIN`, `EINTR`, `ERESTART`, or `EWOULDBLOCK`.
- Design a multi-reader, single-writer subsystem with documented lock order.
- Read a `WITNESS` warning and identify the offending lock pair from the source location alone.
- Diagnose a hung system in DDB using `show all locks` and `show sleepchain`.
- Run a composite stress workload and measure lock contention with `lockstat(1)`.
- Write a `LOCKING.md` document that another developer can use as authoritative reference.

If any of those feels uncertain, the labs in Chapter 12 are the place to build the muscle memory. None requires more than a couple of hours; together they cover every primitive and every pattern the chapter introduced.

### Three Closing Reminders

The first is to *run the composite stress before you commit*. The composite kit catches the cross-primitive bugs that single-axis tests miss. Thirty minutes on a debug kernel is a small investment for the confidence it produces.

The second is to *keep the lock order honest*. Every new lock you introduce starts a new question: where in the order does it sit? Answer the question explicitly in `LOCKING.md` before you write the code. The cost of getting the answer wrong scales with the size of the driver; the cost of writing it down at the start is one minute.

The third is to *trust the primitives and use the right one*. The kernel's mutex, cv, sx, and rw locks are the result of decades of engineering. The temptation to roll your own coordination using flags and atomic flags is real and almost always misguided. Pick the primitive that names what you are trying to say. The code will be shorter, clearer, and provably more correct.



## Reference: The Driver's Stage Progression

Chapter 12 evolves the driver in four discrete stages, each of which is its own directory under `examples/part-03/ch12-synchronization-mechanisms/`. The progression mirrors the chapter's narrative; it lets a reader build the driver one primitive at a time and see what each addition contributes.

### Stage 1: cv-channels

Replaces the anonymous `&sc->cb` wakeup channel with two named condition variables (`data_cv`, `room_cv`). The wait helpers use `cv_wait_sig` instead of `mtx_sleep`. The signallers use `cv_signal` (or `cv_broadcast` in detach) on the cv that matches the state change.

What changes: the sleep/wake mechanism. The driver behaves identically from user space.

What you can verify: `procstat -kk` shows the cv name (`myfirst data` or `myfirst room`) instead of the wmesg (`myfrd`). `dtrace` can attach to specific cvs. Throughput is slightly higher because per-event signalling avoids waking unrelated waiters.

### Stage 2: bounded-read

Adds a `read_timeout_ms` sysctl that bounds blocking reads via `cv_timedwait_sig`. A symmetric `write_timeout_ms` is also possible.

What changes: the read path can now return `EAGAIN` after a configurable timeout. The default of zero preserves the indefinite-wait behaviour from Stage 1.

What you can verify: `timeout_tester` reports `EAGAIN` after approximately the configured timeout. Setting the timeout to zero restores indefinite waits. Ctrl-C still works either way.

### Stage 3: sx-config

Adds a `cfg` struct to the softc, protected by an `sx_lock` (`cfg_sx`). Three configuration fields (`debug_level`, `soft_byte_limit`, `nickname`) are exposed as sysctls. The data path consults `debug_level` for log emission and `soft_byte_limit` for write rejection.

What changes: the driver gains a configuration interface. The `MYFIRST_DBG` macro consults the current debug level. Writes that would exceed the soft limit return `EAGAIN`.

What you can verify: `sysctl -w dev.myfirst.0.debug_level=2` produces visible debug messages. Setting `soft_byte_limit` causes writes to start failing once the buffer reaches the limit. `WITNESS` reports the lock order (mtx first, sx second) and is silent under stress.

### Stage 4: final

The combined version with all three primitives, plus a `LOCKING.md` update, the version bump to `0.6-sync`, and the new `myfirst_sysctl_reset` that exercises both locks together.

What changes: integration. No new primitives.

What you can verify: the regression suite passes; the composite stress workload runs cleanly for at least 30 minutes; `clang --analyze` is silent.

This four-stage progression is the canonical Chapter 12 driver. The companion examples mirror the stages exactly so a reader can compile and load any one of them.



## Reference: Migrating From mtx_sleep to cv

If you are working on an existing driver that uses the legacy `mtx_sleep`/`wakeup` channel mechanism, the migration to `cv(9)` is mechanical. A short recipe.

A note before starting: the legacy mechanism is not deprecated and is still widely used in the FreeBSD tree. Many drivers will keep `mtx_sleep` indefinitely and that is perfectly correct. The migration is worth doing when you have multiple distinct conditions sharing a single channel (the thundering-herd case), or when you want the `procstat` and `dtrace` visibility that named cvs provide. For a driver with a single condition and a single channel, the migration is purely cosmetic; do it for readability if you want, skip it if you do not.

### Step 1: Identify Each Logical Wait Channel

Read the source. Find every `mtx_sleep` call. For each, ask: what is the condition this thread is waiting for?

In the Chapter 11 driver, there were two logical conditions both using `&sc->cb`:

- `myfirst_wait_data`: waiting for `cbuf_used > 0`.
- `myfirst_wait_room`: waiting for `cbuf_free > 0`.

Two conditions; one channel. The migration assigns each its own cv.

### Step 2: Add cv Fields to the Softc

For each logical condition, add a `struct cv` field. Pick a descriptive name:

```c
struct cv  data_cv;
struct cv  room_cv;
```

Initialize in attach (`cv_init`) and destroy in detach (`cv_destroy`).

### Step 3: Replace mtx_sleep with cv_wait_sig

For each `mtx_sleep` call, replace with `cv_wait_sig` (or `cv_timedwait_sig`):

```c
/* Before: */
error = mtx_sleep(&sc->cb, &sc->mtx, PCATCH, "myfrd", 0);

/* After: */
error = cv_wait_sig(&sc->data_cv, &sc->mtx);
```

The wmesg argument is gone (the cv's description string takes its place). The `PCATCH` is implicit in the `_sig` suffix. The interlock argument is the same.

### Step 4: Replace wakeup with cv_signal or cv_broadcast

For each `wakeup(&channel)` call, decide whether one waiter or all waiters should be woken. Replace with `cv_signal` or `cv_broadcast`:

```c
/* Before: */
wakeup(&sc->cb);  /* both readers and writers were on this channel */

/* After: */
if (write_succeeded)
        cv_signal(&sc->data_cv);  /* only readers care about new data */
if (read_succeeded)
        cv_signal(&sc->room_cv);  /* only writers care about new room */
```

This is also the moment to add the per-event correspondence the cv API encourages: signal only when the state actually changed.

### Step 5: Update the Detach Path

Detach was waking the channel before destroying state:

```c
/* Before: */
sc->is_attached = 0;
wakeup(&sc->cb);

/* After: */
sc->is_attached = 0;
cv_broadcast(&sc->data_cv);
cv_broadcast(&sc->room_cv);
/* later, after all waiters have exited: */
cv_destroy(&sc->data_cv);
cv_destroy(&sc->room_cv);
```

`cv_broadcast` ensures every waiter wakes; the post-wait `is_attached` check returns `ENXIO` for each of them.

### Step 6: Update LOCKING.md

Document each new cv: its name, its condition, its interlock, its signaller(s), its waiter(s). The Chapter 12 driver's `LOCKING.md` is the template.

### Step 7: Re-run the Stress Kit

The migration should not change observable behaviour; only the internal mechanism. Run the existing tests; they should pass. Run them under `WITNESS`; no new warnings should appear.

The migration in our chapter is a few hundred lines of source; the recipe above scales to drivers of any size. The benefit is that every wait is now self-documenting and every signal is targeted.

### When Migration Is and Is Not Worth It

The migration costs a few hours of refactor effort, a careful re-run of the test suite, and an update to the documentation. The benefits are:

- Each wait acquires a name visible in `procstat`, `dtrace`, and source.
- Wakeups become per-condition; the thundering herd shrinks.
- Wakeup-channel mismatches are easier to spot in the source.

For a small driver with a single waiting condition, the costs and benefits roughly cancel; the legacy mechanism is fine. For a driver with two or more distinct conditions, the migration almost always pays off. For a driver maintained by multiple developers, the readability win is large.



## Reference: Pre-Production Audit Checklist

A short audit to perform before promoting a synchronization-heavy driver from development to production. Each item is a question; each item should be answerable with confidence.

### Lock Inventory

- [ ] Have I listed every lock the driver owns in `LOCKING.md`?
- [ ] For each lock, have I named what it protects?
- [ ] For each lock, have I named the contexts in which it may be acquired?
- [ ] For each lock, have I documented its lifetime (created where, destroyed where)?

### Lock Order

- [ ] Is the global lock order documented in `LOCKING.md`?
- [ ] Does every code path that holds two locks follow the global order?
- [ ] Have I run `WITNESS` for at least 30 minutes under stress and observed no order reversals?
- [ ] If the driver has more than one instance, have I confirmed that intra-instance ordering is consistent with inter-instance ordering?

### cv Inventory

- [ ] Have I listed every cv the driver owns?
- [ ] For each cv, have I named the condition it represents?
- [ ] For each cv, have I named the interlock mutex?
- [ ] For each cv, have I confirmed at least one signaller and at least one waiter?
- [ ] For each cv, have I confirmed `cv_broadcast` is called in detach before `cv_destroy`?

### Wait Helpers

- [ ] Is every `cv_wait` (or variant) inside a `while (!condition)` loop?
- [ ] Does every wait helper re-check `is_attached` after the wait?
- [ ] Does every wait helper return a sensible error (`ENXIO`, `EINTR`, `EAGAIN`)?

### Signal Sites

- [ ] Does every state change that should wake waiters have a corresponding `cv_signal` or `cv_broadcast`?
- [ ] Is `cv_signal` used when only one waiter needs to wake; `cv_broadcast` only when all do?
- [ ] Are signal sites guarded by `if (state_changed)` so empty signals are skipped?

### Detach Path

- [ ] Does detach refuse to proceed while `active_fhs > 0`?
- [ ] Does detach clear `is_attached` under the device mutex?
- [ ] Does detach broadcast every cv before destroying?
- [ ] Are primitives destroyed in reverse acquisition order (innermost lock destroyed first)?

### Static Analysis

- [ ] Has `clang --analyze` been run; new warnings triaged?
- [ ] Has `WARNS=6` build produced no warnings?
- [ ] Has the regression suite been run on a `WITNESS` kernel; all tests pass?

### Documentation

- [ ] Is `LOCKING.md` up to date with the code?
- [ ] Is the version string in the source bumped?
- [ ] Is the `CHANGELOG.md` updated?
- [ ] Does the `README.md` describe the new features and their sysctls?

A driver that passes this audit is a driver you can trust under load.



## Reference: Sleep Channel Hygiene

Both the legacy `mtx_sleep`/`wakeup` channel mechanism and the modern `cv(9)` API rely on a *channel* that identifies which waiters a wakeup affects. The channel is a key into the kernel's sleep-queue hash table. Mistakes around channels are the source of several common bugs.

A few rules of hygiene.

### One Channel Per Logical Condition

If your driver has two distinct conditions that block (say, "data available" and "room available"), use two distinct channels. Sharing a single channel forces every wakeup to wake all waiters; some of them will go back to sleep immediately because their condition is still false. The performance cost is real; the readability cost is also real.

In our chapter, this rule manifests as `data_cv` and `room_cv` being separate cvs. The Chapter 11 driver used a shared anonymous channel `&sc->cb` and paid the thundering-herd cost; Chapter 12's split is the cure.

### Channel Pointers Must Be Stable

A channel is an address. The kernel does not interpret it; it uses it as a hash key. The address must not change between the wait and the signal. This usually happens automatically (the address of a softc field is stable for the lifetime of the softc), but be careful about temporary buffers, stack-allocated structures, or freed memory.

If you see a wait that hangs after a particular code path, suspect a channel-pointer mismatch. The signaller and the waiter must be using the same address.

### Channel Pointers Must Be Unique to Their Purpose

If the same address is used for two different purposes (say, the data-available channel and a "completion" channel), wakeups for one purpose may unintentionally wake waiters for the other. Use a different field of the softc as the channel for each purpose, or use a cv (which has a name and is a separate object).

### Wakers Should Hold the Interlock When the State Could Change

Although `wakeup` and `cv_signal` are not strictly required to be called under the interlock, doing so closes a race window in which a waiter checks the condition (false), the state changes, the wakeup fires (no waiters in the queue), and the waiter then enqueues and sleeps forever. Holding the interlock while signalling is the safe default; relax only when you can prove the state cannot revert.

Our Chapter 12 design signals after dropping the mutex, which is safe because the cv's own enqueue-under-interlock contract closes the race for cv (not for `wakeup`). For `wakeup`, signal under the mutex.

### A Signal With No Waiter Is Free

`cv_signal` and `wakeup` on a channel with no waiters do nothing. There is no penalty for an unnecessary signal; the cost is essentially the cost of taking and releasing the cv-queue spin lock. Do not avoid signals out of optimization fear; signal when the state changes, even if it sometimes signals nothing.

### A Wait With No Signaller Is a Bug

A wait that is never signalled is a hang. Ensure that every wait has at least one matching signal site, and that the matching site is reached in every code path that produces the awaited state change.

This is the most common cv bug. The audit checklist asks the question; the discipline of asking it during code review catches most cases.



## Reference: Common cv Idioms

Quick-lookup collection for the cv patterns you will use most often.

### Wait for a Condition

```c
mtx_lock(&mtx);
while (!condition)
        cv_wait_sig(&cv, &mtx);
/* condition is true; do work */
mtx_unlock(&mtx);
```

The `while` loop is essential. Spurious wakeups are permitted; signals interrupt the wait; both look like a return from `cv_wait_sig`. Re-check the condition after every return.

### Signal One Waiter

```c
mtx_lock(&mtx);
/* change state */
mtx_unlock(&mtx);
cv_signal(&cv);
```

`cv_signal` after the unlock saves a context switch (the woken thread does not contend immediately for the mutex). Acceptable when the state change is unambiguous and no concurrent path can revert it.

### Broadcast a State Change

```c
mtx_lock(&mtx);
state_changed_globally = true;
cv_broadcast(&cv);
mtx_unlock(&mtx);
```

Use `cv_broadcast` when every waiter needs to know about the change. Detach paths and configuration resets are typical examples.

### Wait With a Timeout

```c
mtx_lock(&mtx);
while (!condition) {
        int ticks = (ms * hz + 999) / 1000;
        int err = cv_timedwait_sig(&cv, &mtx, ticks);
        if (err == EWOULDBLOCK) {
                mtx_unlock(&mtx);
                return (EAGAIN);
        }
        if (err != 0) {
                mtx_unlock(&mtx);
                return (err);
        }
}
/* do work */
mtx_unlock(&mtx);
```

Convert milliseconds to ticks, round up, handle the three return cases (timeout, signal, normal wakeup) explicitly.

### Wait With Detach Awareness

```c
while (!condition) {
        int err = cv_wait_sig(&cv, &mtx);
        if (err != 0)
                return (err);
        if (!sc->is_attached)
                return (ENXIO);
}
```

The post-sleep `is_attached` check ensures we exit cleanly if the device was detached while we slept. `cv_broadcast` in the detach path makes this work.

### Drain Waiters Before Destroy

```c
mtx_lock(&mtx);
sc->is_attached = 0;
cv_broadcast(&cv);
mtx_unlock(&mtx);
/* waiters wake, see !is_attached, return ENXIO, exit */
/* by the time active_fhs == 0, no waiters remain */
cv_destroy(&cv);
```

The combination of broadcast and the `is_attached` re-check guarantees no waiter remains in the cv at destroy time.



## Reference: Common sx Idioms

### Read-Mostly Field

```c
sx_slock(&sx);
value = field;
sx_sunlock(&sx);
```

Cheap on a multi-core system; multiple readers do not contend.

### Update With Validation

```c
sx_slock(&sx);
old = field;
sx_sunlock(&sx);

/* validate possibly-new value with sysctl_handle_*, etc. */

sx_xlock(&sx);
field = new;
sx_xunlock(&sx);
```

Two acquire-release cycles. The shared lock for the read; the exclusive lock for the write. Drop between them so the validation does not hold either lock.

### Snapshot-and-Apply Across Two Locks

```c
sx_slock(&cfg_sx);
local = cfg.value;
sx_sunlock(&cfg_sx);

mtx_lock(&data_mtx);
/* use local without holding either lock together */
mtx_unlock(&data_mtx);
```

Avoids holding both locks simultaneously; relaxes the lock-order constraint.

### Try-Upgrade Pattern

```c
sx_slock(&sx);
if (need_modify) {
        if (sx_try_upgrade(&sx)) {
                /* exclusive */
                modify();
                sx_downgrade(&sx);
        } else {
                /* drop, reacquire as exclusive, re-validate */
                sx_sunlock(&sx);
                sx_xlock(&sx);
                if (still_need_modify())
                        modify();
                sx_downgrade(&sx);
        }
}
sx_sunlock(&sx);
```

Optimistic upgrade. The fallback path must re-validate, because the world changed during the unlock window.

### Assert Held

```c
sx_assert(&sx, SA_SLOCKED);  /* shared */
sx_assert(&sx, SA_XLOCKED);  /* exclusive */
sx_assert(&sx, SA_LOCKED);   /* either */
```

Use at the start of helpers that expect a particular lock state.



## Reference: Decision Table for Synchronization Primitives

A compact lookup table.

| If you need to... | Use |
|---|---|
| Update a single word atomically | `atomic(9)` |
| Update a per-CPU counter cheaply | `counter(9)` |
| Protect composite state in process context | `mtx(9)` (`MTX_DEF`) |
| Protect composite state in interrupt context | `mtx(9)` (`MTX_SPIN`) |
| Protect read-mostly state in process context | `sx(9)` |
| Protect read-mostly state where sleep is forbidden | `rw(9)` |
| Wait for a specific condition to become true | `cv(9)` (paired with `mtx(9)` or `sx(9)`) |
| Wait until a deadline | `cv_timedwait_sig`, or `mtx_sleep` with `timo` > 0 |
| Wait such that Ctrl-C can interrupt | The `_sig` variant of any wait primitive |
| Run code at a specific time in the future | `callout(9)` (Chapter 13) |
| Defer work to a worker thread | `taskqueue(9)` (Chapter 16) |
| Read concurrently with no synchronization at all | `epoch(9)` (later chapters) |

If two primitives both fit, use the simpler one.



## Reference: Reading kern_condvar.c and kern_sx.c

Two files in `/usr/src/sys/kern/` are worth opening once you have used the cv and sx APIs in your driver.

`/usr/src/sys/kern/kern_condvar.c` is the cv implementation. The functions worth seeing:

- `cv_init`: initialization. Trivial.
- `_cv_wait` and `_cv_wait_sig`: the core blocking primitives. Each takes the cv-queue spin lock, increments the waiter count, drops the interlock, hands the thread to the sleep queue, yields, and on return reacquires the interlock. The atomicity of "drop interlock, sleep" is provided by the sleep queue layer.
- `_cv_timedwait_sbt` and `_cv_timedwait_sig_sbt`: the timed variants. Same shape, with a callout that wakes the thread if the timeout fires first.
- `cv_signal`: takes the cv-queue spin lock, signals one waiter via `sleepq_signal`.
- `cv_broadcastpri`: signals all waiters at the given priority.

The whole file is about 400 lines. An afternoon of reading is more than enough to understand it end to end.

`/usr/src/sys/kern/kern_sx.c` is the sx implementation. Larger and denser, because the lock supports both shared and exclusive modes with full priority propagation. The functions worth seeing:

- `sx_init_flags`: initialization. Sets the initial state, registers with `WITNESS`.
- `_sx_xlock_hard` and `_sx_xunlock_hard`: the slow paths for exclusive operations. The fast paths inline in `sx.h`.
- `_sx_slock_int` and `_sx_sunlock_int`: the shared-mode operations. The shared count is incremented via atomic compare-and-swap; if the lock is held exclusively, the thread blocks.
- `sx_try_upgrade_int` and `sx_downgrade_int`: the mode-change operations.

Skim. The internals are intricate, but the public-facing API behaves as documented and the source confirms it.



## Reference: Common Mistakes With cv and sx

Each new primitive comes with a set of mistakes that beginners make until they have been bitten. A short catalogue.

### cv Mistakes

**Using `if` instead of `while` around `cv_wait`.** The condition may not be true on return because of a spurious wakeup. Always loop.

**Forgetting to broadcast in detach.** Waiters never wake, the cv has lingering waiters at destroy time, the kernel may panic. Always `cv_broadcast` before `cv_destroy`.

**Signalling the wrong cv.** Waking up readers when you meant writers (or vice versa). Easy mistake when refactoring. The cv's name is your defense; if `cv_signal(&sc->room_cv)` does not feel right at the call site, it probably is not right.

**Signalling without the interlock when the state could revert.** If two threads can both modify the state, one of them must hold the interlock when signalling, or a wakeup may be lost. Default to signalling under the interlock; relax only when you can prove the state cannot revert.

**Missing the post-wait check for detach.** A waiter that wakes up because of a `cv_broadcast` from detach must re-check `is_attached` and return `ENXIO`. If the check is missing, the waiter proceeds as if the device is still alive and crashes.

**Calling cv_wait while holding multiple locks.** Only the interlock is dropped during the sleep. Other locks remain held. If those locks are needed by the waker, you have a deadlock. Drop other locks first.

### sx Mistakes

**Holding the sx across a sleeping call.** Drop before `sysctl_handle_*`, `uiomove`, or `malloc(M_WAITOK)`. The sx is sleepable, so the kernel will not panic, but other waiters will be blocked for the duration.

**Acquiring shared and then xlock without dropping shared.** `sx_xlock` while holding the same sx in shared mode is a deadlock; the call will block forever waiting for itself. Use `sx_try_upgrade` or drop and reacquire.

**Forgetting that sx is sleepable.** Calling `sx_xlock` from a context where sleeping is illegal (interrupt context, inside a spin lock) panics. Use `rw(9)` for those contexts.

**Holding the sx in shared mode across a long operation.** Other readers can proceed, but the sx writer is blocked indefinitely. If the operation is long, drop the shared lock, do the work, and reacquire if you need to commit.

**Releasing the wrong mode.** `sx_xunlock` on a shared-mode lock is a bug; `sx_sunlock` on an exclusive-mode lock is a bug. Use `sx_unlock` (the polymorphic version) only when you do not know which mode you are in (rare).

### Mistakes Specific to Combining Both

**Acquiring in the wrong order.** The Chapter 12 driver requires mtx first, sx second. Reverse order produces a `WITNESS` warning under load.

**Releasing in the wrong order.** Acquire mtx, acquire sx, release mtx, release sx. The release order *must* be the reverse of the acquisition order: release sx first, then release mtx. Otherwise an observer between the two releases sees an unexpected combination.

**Snapshot-and-apply where staleness matters.** The pattern is correct only when the snapshot can tolerate small staleness. For values that must be current (security flags, hard-quota limits), snapshot-and-apply is wrong; you must hold both locks atomically.

**Forgetting to update LOCKING.md.** Adding a lock or changing the order without updating the documentation produces drift. Three months later, no one remembers what the rule was. Update the document in the same commit.



## Reference: Time Primitives

A brief tour of how the kernel expresses time. Useful when reading or writing the timed-wait variants.

The kernel has three commonly-used time representations:

- `int` ticks. The legacy unit. `hz` ticks equal one second. The default `hz` on FreeBSD 14.3 is 1000, so one tick is one millisecond. `mtx_sleep`, `cv_timedwait`, and `tsleep` all take their timeouts in ticks.
- `sbintime_t`. A 64-bit signed binary fixed-point representation: the upper 32 bits are seconds, the lower 32 bits are a fraction of a second. The unit constants are in `/usr/src/sys/sys/time.h`: `SBT_1S`, `SBT_1MS`, `SBT_1US`, `SBT_1NS`. The newer time API (`msleep_sbt`, `cv_timedwait_sbt`, `callout_reset_sbt`) uses sbintime.
- `struct timespec`. POSIX seconds-and-nanoseconds. Used at the user-space boundary; rarely needed in driver internals.

Conversion helpers in `time.h`:

- `tick_sbt`: a global variable holding `1 / hz` as an sbintime, so `tick_sbt * timo_in_ticks` gives the equivalent sbintime.
- `nstosbt(ns)`, `ustosbt(us)`, `sbttous(sbt)`, `sbttons(sbt)`, `tstosbt(ts)`, `sbttots(ts)`: explicit conversions between the various units.

The `_sbt` time API exists because `hz` granularity is too coarse for some uses. With `hz=1000`, the smallest expressible timeout is 1 ms, and timeouts are aligned to tick boundaries. With sbintime, you can express 100 microseconds and ask the kernel to schedule the wakeup as close to that as the hardware timer allows.

For Chapter 12, we use the tick-based API everywhere because the precision is sufficient. The reference is here so you know where to reach when sub-millisecond precision matters.

The `pr` argument to `_sbt` functions deserves a sentence. It is the *precision* the caller is willing to accept: how much wiggle the kernel may add for power-saving timer coalescing. A precision of `SBT_1S` means "I do not care if my 5-second timer fires up to 1 second late; if you can coalesce it with another timer to save power, please do". A precision of `SBT_1NS` means "fire as close to the deadline as you can". For driver code, `0` (no slop) or `SBT_1MS` (a millisecond of slop) are the typical values.

The `flags` argument controls how the timer is registered. `C_HARDCLOCK` is the most common: align to the system's hardclock interrupt for predictable timing. `C_DIRECT_EXEC` runs the callout in the timer interrupt rather than deferring it to a callout thread. `C_ABSOLUTE` interprets `sbt` as an absolute time rather than a relative timeout. We use `C_HARDCLOCK` everywhere in Chapter 12.



## Reference: Common WITNESS Warnings, Decoded

`WITNESS` produces several kinds of warning. Each has a recognisable shape.

### "lock order reversal"

The signature: two lines naming "1st" and "2nd" lock, plus an "established at" line. We have walked through the diagnosis in Section 6.

Common cause: a path that takes locks in an order that contradicts a previously-observed order. Fix by reordering or restructuring.

### "duplicate lock of same name"

The signature: a warning about acquiring a lock with the same `lo_name` as one already held.

Common cause: two instances of the same driver, each with its own lock, both with the same name. `WITNESS` is conservative and assumes that two locks of the same type belong to the same class. Fix by initializing each lock with a unique name (e.g., include the unit number via `device_get_nameunit(dev)`), or by passing the appropriate per-class "duplicate-acquire OK" flag at init time: `MTX_DUPOK` for mutexes, `SX_DUPOK` for sx, `RW_DUPOK` for rwlocks. Each of those expands to the lock-object-level `LO_DUPOK` bit; you write the per-class name in driver code.

### "sleeping thread (pid N) owns a non-sleepable lock"

The signature: a thread is in a sleeping primitive (`cv_wait`, `mtx_sleep`, `_sleep`) while holding a spin mutex or an rw lock.

Common cause: a function that takes a non-sleepable lock and then calls something that may sleep. Fix by dropping the non-sleepable lock first.

### "exclusive sleep mutex foo not owned at"

The signature: a thread tried to release or assert a mutex it does not hold.

Common cause: the wrong mutex pointer, or an unlock without a matching lock on this code path. Fix by tracing the lock acquisition.

### "lock list reversal"

The signature: similar to lock-order reversal but indicates a more complex inversion involving more than two locks.

Common cause: a chain of acquisitions that taken together violates the global order. Fix by simplifying the acquisition pattern; if the chain is truly necessary, consider whether the design should use fewer locks.

### "sleepable acquired while holding non-sleepable"

The signature: a thread tried to acquire a sleepable lock (sx, mtx_def, lockmgr) while holding a non-sleepable one (mtx_spin, rw).

Common cause: confusion about the lock classes. Fix by switching the inner lock to a sleepable variant or by restructuring to avoid the nesting.

### Acting on a Warning

When `WITNESS` fires, the temptation is to suppress the warning. Resist. The warning means the kernel has observed a real situation that violates a real rule. Suppression hides the bug; it does not fix it.

The right responses, in order of preference:

1. Fix the bug (reorder locks, drop a lock, restructure code).
2. Explain why the warning is incorrect for this case and use the appropriate `_DUPOK` flag with a comment in the source.
3. If you cannot do either, escalate. Ask on freebsd-hackers or open a PR. A `WITNESS` warning that nobody can explain is a real bug somewhere.



## Reference: Lock Class Quick Reference

A compact lookup for the differences between the lock classes you have seen so far.

| Property | `mtx_def` | `mtx_spin` | `sx` | `rw` | `rmlock` | `lockmgr` |
|---|---|---|---|---|---|---|
| Sleeps when contended | Yes | No (spins) | Yes | No (spins) | No (mostly) | Yes |
| Multiple holders | No | No | Yes (shared) | Yes (read) | Yes (read) | Yes (shared) |
| Holder may sleep | Yes | No | Yes | No | No (read) | Yes |
| Priority propagation | Yes | n/a | No | Yes | n/a | Yes |
| Signal interruptible | n/a | n/a | `_sig` | No | No | Yes |
| Recursion supported | Optional | Yes | Optional | No | No | Yes |
| WITNESS-tracked | Yes | Yes | Yes | Yes | Yes | Yes |
| Best driver use | Default | Interrupt context | Read-mostly | Hot read paths | Very hot reads | Filesystems |

`rmlock(9)` and `lockmgr(9)` are listed for completeness; this book covers `mtx`, `cv`, `sx`, and `rw` in depth, and treats the others as "known to exist, look up the manual page if needed".



## Reference: Multi-Primitive Driver Design Patterns

Three patterns recur in drivers that combine several synchronization primitives. Each is worth a sentence so you recognize them in the wild.

### Pattern: One Mutex, One Configuration sx

The `myfirst` driver of Chapter 12 is this pattern. The mutex protects the data path and a sx protects the configuration. The lock order is mutex first, sx second. Most simple drivers fit this pattern.

When to use: the data path is process-context, has a composite invariant, and reads configuration occasionally. The configuration is read frequently and written rarely.

When not to use: when the data path runs in interrupt context (mutex must be `MTX_SPIN`, configuration must be `rw`), or when the data path itself has sub-paths that benefit from different locks.

### Pattern: Per-Queue Lock With a Configuration Lock

A driver with multiple queues (one per CPU, one per consumer, one per stream) gives each queue its own lock and uses a separate sx for configuration. The lock order is per-queue lock first, configuration sx second. Lock-order between queues is not defined (you should never hold two queues at once).

When to use: high core counts, the workload partitions naturally per queue.

When not to use: the workload is symmetric and the per-queue locks would not help, or the data crosses queues frequently and would force order rules.

### Pattern: Per-Object Lock With a Container Lock

A driver that maintains a list of objects (devices, sessions, descriptors) gives each object its own lock and uses a container lock to protect the list of objects. Walking the list takes the container lock; modifying an object takes that object's lock; both can be held in the order container first, object second.

When to use: list operations and per-object operations both need protection, with different lifetimes.

When not to use: a single mutex would be enough (small lists, infrequent operations).

The `myfirst` driver does not need this pattern yet; future drivers in the book will.

### Pattern: Per-CPU Counters With Mutex-Protected Tail

This is the Chapter 11 pattern, which Chapter 12 inherits. Hot counters (bytes_read, bytes_written) use `counter(9)` per-CPU storage. The cbuf, with its composite invariant, uses a single mutex. The two are independent; updates to the counters do not need the mutex; updates to the cbuf still do.

When to use: a high-frequency counter sits next to a structure with a composite invariant.

When not to use: the counter updates need to be consistent with the structure updates (then both need the same lock).

### Pattern: Snapshot-and-Apply Across Two Lock Classes

Any time a path needs both lock classes, the snapshot-and-apply pattern reduces the lock-order constraint to a single direction. Read from one lock, drop, then take the other. The snapshot may be slightly stale; for advisory values, that is acceptable.

When to use: the value being snapshot is not strictly current-required; staleness on the order of microseconds is acceptable.

When not to use: the value is a security flag, a hard-budget limit, or anything where staleness could violate a contract.

The `myfirst_write` path uses this for the soft byte limit: snapshot the limit under the cfg sx, drop it, take the data mutex, check the limit against the current `cb_used`. The combined operation is not atomic, but it is correct in the sense that any race causes the wrong answer to be a tolerable wrong answer (refuse a write that would have fit, or accept a write that just barely overflows; both are recoverable).



## Reference: Pre-Conditions for Each Primitive

Each primitive has rules about when and how it may be used. Violating a rule is a bug; the rules are listed here for quick reference.

### mtx(9) (MTX_DEF)

Pre-conditions for `mtx_init`:
- Memory for the `struct mtx` exists and is not aliased.
- The mutex is not already initialized.

Pre-conditions for `mtx_lock`:
- Mutex is initialized.
- Calling thread is in process context, kernel-thread context, or callout-mpsafe context.
- Calling thread does not already hold the mutex (unless `MTX_RECURSE`).
- Calling thread does not hold any spin mutex.

Pre-conditions for `mtx_unlock`:
- Calling thread holds the mutex.

Pre-conditions for `mtx_destroy`:
- Mutex is initialized.
- No thread holds the mutex.
- No thread is blocked on the mutex.

### cv(9)

Pre-conditions for `cv_init`:
- Memory for the `struct cv` exists.
- The cv is not already initialized.

Pre-conditions for `cv_wait` and `cv_wait_sig`:
- The interlock mutex is held by the calling thread.
- Calling thread is in a context where sleeping is legal.

Pre-conditions for `cv_signal` and `cv_broadcast`:
- The cv is initialized.
- Convention: the interlock mutex is held by the calling thread (not strictly required by the API, but defensive).

Pre-conditions for `cv_destroy`:
- The cv is initialized.
- No thread is blocked on the cv (the wait queue must be empty).

### sx(9)

Pre-conditions for `sx_init`:
- Memory for the `struct sx` exists.
- The sx is not already initialized (unless `SX_NEW` is used).

Pre-conditions for `sx_xlock` and `sx_xlock_sig`:
- The sx is initialized.
- Calling thread does not already hold the sx exclusively (unless `SX_RECURSE`).
- Calling thread is in a context where sleeping is legal.
- Calling thread does not hold any non-sleepable lock (no spin mutex, no rw lock).

Pre-conditions for `sx_slock` and `sx_slock_sig`:
- Same as `sx_xlock` except recursion check applies to shared mode.

Pre-conditions for `sx_xunlock` and `sx_sunlock`:
- Calling thread holds the sx in the corresponding mode.

Pre-conditions for `sx_destroy`:
- The sx is initialized.
- No thread holds the sx in any mode.
- No thread is blocked on the sx.

### rw(9)

Pre-conditions for `rw_init`, `rw_destroy`: same shape as `sx_init`, `sx_destroy`.

Pre-conditions for `rw_wlock` and `rw_rlock`:
- The rw is initialized.
- Calling thread does not currently hold the rw in a conflicting mode.
- Calling thread is *not* required to be in a sleepable context. The rw lock itself does not sleep; however, the calling thread *must not* call any function that may sleep while the rw is held.

Pre-conditions for `rw_wunlock` and `rw_runlock`:
- Calling thread holds the rw in the corresponding mode.

Following these pre-conditions is the difference between a driver that runs cleanly under `WITNESS` for years and one that produces an unexpected panic on the first uncommon code path.



## Reference: A Chapter 12 Self-Assessment

Use this rubric to confirm you have internalised the Chapter 12 material before turning to Chapter 13. Every question should be answerable without re-reading the chapter.

### Conceptual Questions

1. **Name the three primary forms of synchronization.** Mutual exclusion, shared access with restricted writes, coordinated waiting.

2. **Why is a condition variable preferable to an anonymous wakeup channel?** Each cv represents one logical condition; signals do not wake unrelated waiters; the cv has a name visible in `procstat` and `dtrace`; the API enforces the atomic drop-and-sleep contract through its types.

3. **What is the difference between cv_signal and cv_broadcast?** `cv_signal` wakes one waiter (highest priority, FIFO among equal); `cv_broadcast` wakes all waiters. Use signal for per-event state changes; use broadcast for global changes (detach, reset).

4. **What does cv_wait_sig return when interrupted by a signal?** Either `EINTR` or `ERESTART`, depending on the signal's restart disposition. The driver passes the value through unchanged.

5. **What is the difference between sx and rw locks?** `sx(9)` is sleepable; `rw(9)` is not. Use `sx` in process context where the critical section may include sleeping calls; use `rw` when the critical section may run in interrupt context or must not sleep.

6. **Why does sx_try_upgrade exist instead of an unconditional sx_upgrade?** Because two simultaneous holders both attempting an unconditional upgrade would deadlock. The `try` variant returns failure when another shared holder is present, letting the caller back off cleanly.

7. **What is the snapshot-and-apply pattern and why is it useful?** Acquire one lock, read needed values into local variables, release; then acquire a different lock and use the local values. Avoids holding two locks simultaneously, relaxing lock-order constraints. Acceptable when the snapshot can tolerate small staleness.

8. **What is the canonical lock order in the Chapter 12 driver?** sc->mtx before sc->cfg_sx. Documented in `LOCKING.md`; enforced by `WITNESS`.

### Code Reading Questions

Open your Chapter 12 driver source and verify:

1. Every `cv_wait_sig` is inside a `while (!condition)` loop.
2. Every cv has at least one signaller and one broadcast caller (broadcast in detach is acceptable).
3. Every `sx_xlock` has a matching `sx_xunlock` on every code path, including error returns.
4. The detach path broadcasts each cv before destroying any primitive.
5. The cfg sx is dropped before any potentially-sleeping call (`sysctl_handle_*`, `uiomove`, `malloc(M_WAITOK)`).
6. The lock order rule (mtx first, sx second) is followed in every path that holds both.

### Hands-On Questions

1. Load the Chapter 12 driver on a `WITNESS` kernel and run the composite stress for 30 minutes. Are there any warnings? If yes, investigate.

2. Set `read_timeout_ms` to 100 and run a `read(2)` against an idle device. What does the call return? After how long?

3. Toggle `debug_level` between 0 and 3 with `sysctl -w` while `mp_stress` is running. Does the level take effect promptly? Does anything break?

4. Use `lockstat(1)` to measure the contention on the sx lock under a config-writer-heavy workload. What is the wait time?

5. Open the kern_condvar.c source and find the function `cv_signal`. Read it. Can you describe what it does in two sentences?

If all five hands-on questions pass and the conceptual questions are easy, your Chapter 12 work is solid.



## Reference: Further Reading on Synchronization

For readers who want to go deeper than this chapter covers:

### Manual Pages

- `mutex(9)`: the mutex API (covered fully in Chapter 11; reference here for completeness).
- `condvar(9)`: the condition variable API.
- `sx(9)`: the shared/exclusive lock API.
- `rwlock(9)`: the reader/writer lock API.
- `rmlock(9)`: the read-mostly lock API (advanced).
- `sema(9)`: the counting semaphore API (advanced).
- `epoch(9)`: the deferred-reclamation read-mostly framework (advanced; relevant for network drivers).
- `locking(9)`: an overview of FreeBSD's locking primitives.
- `lock(9)`: the common lock-object infrastructure.
- `witness(4)`: the WITNESS lock-order checker (covered in Chapter 11; revisited in this chapter).
- `lockstat(1)`: the lock-profiling user-space tool.
- `dtrace(1)`: the dynamic tracing framework, covered in more depth in Chapter 15.

### Source Files

- `/usr/src/sys/kern/kern_condvar.c`: the cv implementation.
- `/usr/src/sys/kern/kern_sx.c`: the sx implementation.
- `/usr/src/sys/kern/kern_rwlock.c`: the rw implementation.
- `/usr/src/sys/kern/subr_sleepqueue.c`: the sleep-queue machinery underlying cv and other sleep primitives.
- `/usr/src/sys/kern/subr_turnstile.c`: the turnstile machinery underlying rw and other priority-propagating primitives.
- `/usr/src/sys/sys/condvar.h`, `/usr/src/sys/sys/sx.h`, `/usr/src/sys/sys/rwlock.h`: the public API headers.
- `/usr/src/sys/sys/_lock.h`, `/usr/src/sys/sys/lock.h`: the common lock-object structure and class registry.

### External Material

For concurrency theory applicable to any operating system, *The Art of Multiprocessor Programming* by Herlihy and Shavit is excellent. For FreeBSD-specific kernel internals, *The Design and Implementation of the FreeBSD Operating System* by McKusick and others remains the canonical textbook; the chapters on locking and scheduling are particularly relevant.

Neither book is required for this chapter. Both are useful when the time comes for deeper study.



## Looking Ahead: Bridge to Chapter 13

Chapter 13 is titled *Timers and Delayed Work*. Its scope is the kernel's time infrastructure as seen from a driver: how to schedule a callback for some time in the future, how to cancel one cleanly, how to handle the rules around callouts that may run concurrently with the driver's other code paths, and how to use timers for typical driver patterns like watchdogs, deferred work, and periodic polling.

Chapter 12 prepared the ground in three specific ways.

First, you already know how to wait with a timeout. Chapter 13's `callout(9)` mechanism is the same idea seen from the other side: instead of "wake me at time T", it is "run this function at time T". The synchronization rules around callouts (callouts run on a kernel thread, can race with your other code, must be drained before destruction) build on the discipline Chapter 12 established for cvs and sxs.

Second, you already know how to design a multi-primitive driver. Chapter 13's callouts add another execution context to the driver: the callout handler runs concurrently with `myfirst_read`, `myfirst_write`, and the sysctl handlers. That means callout handlers participate in the lock order. The `LOCKING.md` you wrote in Chapter 12 will absorb the addition with one new entry.

Third, you already know how to debug under load. Chapter 13 introduces a new bug class (callout races at unload time) that benefits from the same `WITNESS`, `lockstat`, and `dtrace` workflow Chapter 12 taught.

Specific topics Chapter 13 will cover include:

- The `callout(9)` API: `callout_init`, `callout_init_mtx`, `callout_reset`, `callout_stop`, `callout_drain`.
- The lock-aware callout (`callout_init_mtx`) and why it is the right default for driver code.
- Callout reuse: scheduling the same callout multiple times safely.
- The unload race: how a callout that fires after `kldunload` can crash the kernel, and how to prevent it with `callout_drain`.
- Periodic patterns: the watchdog, the heartbeat, the deferred reaper.
- The `tick_sbt` and `sbintime_t` time abstractions, useful for sub-millisecond timing.
- Comparison with `timeout(9)` (the older interface, deprecated for new code).

You do not need to read ahead. Chapter 12's material is sufficient preparation. Bring your Chapter 12 driver, your test kit, and your `WITNESS`-enabled kernel. Chapter 13 starts where Chapter 12 ended.

A small closing reflection. You started this chapter with one mutex, one anonymous channel, and a clear idea of what synchronization meant. You leave with three primitives, a documented lock order, a richer vocabulary, and the experience of debugging real coordination problems with real kernel tools. That progression is the heart of Part 3 of this book. From here, Chapter 13 expands the driver's awareness of *time*, Chapter 14 expands its awareness of *interrupts*, and the remaining chapters of Part 3 prepare you for the hardware-touching chapters of Part 4.

Take a moment. The driver you started Part 3 with knew only how to handle one thread at a time. The driver you have now coordinates many threads across two lock classes, can be reconfigured at runtime without disrupting its data path, and respects the user's signals and deadlines. That is a real, qualitative leap. Then turn the page.

When you do open Chapter 13, the first thing you will see is `callout(9)`, the kernel's timed-callback infrastructure. The discipline you learned here for cvs, sxs, and the lock-order-aware design transfers directly. Callouts are simply another concurrent execution context that participates in the lock order; the patterns from Chapter 12 absorb them without growing brittle. The synchronization vocabulary is the same; the time vocabulary is what is new.
