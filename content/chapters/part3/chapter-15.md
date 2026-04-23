---
title: "More Synchronization: Conditions, Semaphores, and Coordination"
description: "The last mile of Part 3: counting semaphores for admission control, refined sx(9) patterns for read-mostly state, interruptible and timeout-aware waits, cross-component handshakes, and a wrapper layer that makes the driver's synchronization story something a future maintainer can actually read."
partNumber: 3
partName: "Concurrency and Synchronization"
chapter: 15
lastUpdated: "2026-04-19"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "TBD"
estimatedReadTime: 165
---

# More Synchronization: Conditions, Semaphores, and Coordination

## Reader Guidance & Outcomes

At the end of Chapter 14 your `myfirst` driver reached a qualitatively different state from where it started Part 3. It has a documented data-path mutex, two condition variables, a configuration sx lock, three callouts, a private taskqueue with three tasks, and a detach path that drains every primitive in the correct order. The driver is, for the first time in the book, more than a collection of handlers. It is a composition of synchronisation primitives that cooperate to deliver bounded, safe behaviour under load.

Chapter 15 is about pushing that composition further. Most real drivers eventually discover that mutexes and basic condition variables, even combined with sx locks and taskqueues, are not always the primitives that make a particular problem easy to express. A driver might need to cap the number of concurrent writers, enforce a bounded pool of reusable hardware slots, coordinate a handshake between a callout and a task, let a slow read wake up for a signal without losing the partial progress it made, or surface shutdown state across several subsystems in a way that every piece of code can check cheaply. Each of those shapes is solvable with what you already know, but each has a primitive or idiom that makes the solution direct and the code readable. This chapter teaches those primitives and idioms one at a time, applies them to the driver, and ties the result together with a small wrapper layer that turns scattered calls into a named vocabulary.

The chapter is also the last chapter of Part 3. After Chapter 15, Part 4 begins and the book turns to hardware. Every primitive Part 3 taught you, from the first `mtx_init` in Chapter 11 to the last `taskqueue_drain` in Chapter 14, stays with you into Part 4. The coordination patterns in this chapter are not a bonus topic. They are the closing piece of the synchronisation toolbox your driver carries forward into the hardware-facing chapters.

### Why This Chapter Earns Its Own Place

You could skip this chapter. The driver as it stands at the end of Chapter 14 is functional, tested, and technically correct. Its mutex-and-cv discipline is sound. Its detach ordering works. Its taskqueue is clean.

What the driver lacks, and what Chapter 15 adds, is a small set of sharper tools for specific shapes of coordination that mutexes and basic condition variables express awkwardly. A counting semaphore is a few lines of code that says "at most N participants at once"; expressing the same invariant with a mutex and a counter and a cv requires more lines and hides the intent. A refined sx pattern with `sx_try_upgrade` lets a read path occasionally promote to a writer without releasing its slot and racing with other would-be writers; without the primitive, you write awkward retry loops. A proper `cv_timedwait_sig` usage distinguishes between EINTR and ERESTART and between "the caller was interrupted" and "the deadline fired"; a naive wait leaves the caller hanging or abandons partial work on any signal.

The payoff of learning these tools is not just that the current chapter's refactor will be cleaner. It is that when you read a production FreeBSD driver a year from now, you will recognise these shapes immediately. When `/usr/src/sys/dev/hyperv/storvsc/hv_storvsc_drv_freebsd.c` calls `sema_wait` on a per-request semaphore to block until hardware completion, you will know what the author was thinking. When a network driver reaches for `sx_try_upgrade` in a statistics-update path, you will know why that was the correct call. Without Chapter 15 those calls are opaque. With Chapter 15 they are obvious.

The other payoff is maintainability. A driver that scatters its synchronisation vocabulary across a hundred places is hard to change. A driver that encapsulates its synchronisation in a small named layer (even just a set of inline functions in a header) is easy to change. Section 6 walks through the encapsulation explicitly; by the end of the chapter, your driver will have a small `myfirst_sync.h` that names every coordination primitive it uses. Adding a new synchronised state later becomes an exercise in extending the header, not in spreading new `mtx_lock`/`mtx_unlock` calls across the file.

### Where Chapter 14 Left the Driver

A few prerequisites to verify before starting. Chapter 15 extends the driver produced at the end of Chapter 14 Stage 4 (version `0.8-taskqueues`). If any of the items below feels uncertain, return to Chapter 14 before starting this chapter.

- Your `myfirst` driver compiles cleanly and identifies itself as version `0.8-taskqueues`.
- It uses `MYFIRST_LOCK`/`MYFIRST_UNLOCK` macros around `sc->mtx` (the data-path mutex).
- It uses `MYFIRST_CFG_SLOCK`/`MYFIRST_CFG_XLOCK` around `sc->cfg_sx` (the configuration sx).
- It uses two named condition variables (`sc->data_cv`, `sc->room_cv`) for blocking reads and writes.
- It supports timed reads via `cv_timedwait_sig` and the `read_timeout_ms` sysctl.
- It has three callouts (`heartbeat_co`, `watchdog_co`, `tick_source_co`) with their interval sysctls.
- It has a private taskqueue (`sc->tq`) with three tasks (`selwake_task`, `bulk_writer_task`, `reset_delayed_task`).
- The lock order `sc->mtx -> sc->cfg_sx` is documented in `LOCKING.md` and enforced by `WITNESS`.
- `INVARIANTS`, `WITNESS`, `WITNESS_SKIPSPIN`, `DDB`, `KDB`, and `KDB_UNATTENDED` are enabled in your test kernel; you have built and booted it.
- The Chapter 14 stress kit runs cleanly under the debug kernel.

That driver is what we extend in Chapter 15. The additions are modest in volume but substantial in what they enable. The driver's data path is unchanged at the mechanical level; what changes is the vocabulary it uses to talk about concurrency.

### What You Will Learn

After finishing this chapter you will be able to:

- Recognise when a mutex plus a condition variable is not the right primitive for a particular invariant, and name the alternative (a semaphore, an sx upgrade pattern, an atomic flag with a memory barrier, a per-cpu counter, or an encapsulated coordinator function).
- Explain what a counting semaphore is, how it differs from a mutex and from a binary semaphore, and why FreeBSD's `sema(9)` API is specifically a counting-semaphore API with no ownership concept.
- Use `sema_init`, `sema_wait`, `sema_post`, `sema_trywait`, `sema_timedwait`, `sema_value`, and `sema_destroy` correctly, including the lifetime contract that no waiters may be present when `sema_destroy` is called.
- Describe the known limitations of FreeBSD's kernel semaphore: no priority inheritance, no signal-interruptible wait, and the guidance in `/usr/src/sys/kern/kern_sema.c` about why they are not a general replacement for a mutex plus a condition variable.
- Refine the driver's sx usage with `sx_try_upgrade`, `sx_downgrade`, `sx_xlocked`, and `sx_slock` patterns that express read-mostly workloads cleanly.
- Distinguish `cv_wait`, `cv_wait_sig`, `cv_timedwait`, and `cv_timedwait_sig`, and know what each returns on timeout, signal, and normal wakeup.
- Handle the EINTR and ERESTART return values from signal-interruptible waits correctly, so that `read(2)` and `write(2)` on the driver respond sensibly to `SIGINT` and friends.
- Build cross-component handshakes between a callout, a task, and a user thread using a small state flag protected by the driver mutex.
- Introduce a `myfirst_sync.h` header that names every synchronisation primitive the driver uses, so future contributors can change the locking strategy in one place.
- Use the `atomic(9)` API correctly for small lock-free coordination steps, especially shutdown flags that need to be visible across contexts without a lock.
- Write stress tests that deliberately trigger race conditions in the driver's synchronisation and confirm the primitives handle them.
- Refactor the driver to version `0.9-coordination` and update `LOCKING.md` with a Semaphores section and a Coordination section.

That is a long list. Each item in it is small; the value of the chapter is in the composition.

### What This Chapter Does Not Cover

Several adjacent topics are explicitly deferred so Chapter 15 stays focused.

- **Hardware interrupt handlers and the full split between `FILTER` and `ITHREAD` execution contexts.** Part 4 introduces `bus_setup_intr(9)` and the actual interrupt story. Chapter 15 mentions interrupt-adjacent contexts only when they illustrate a synchronisation pattern you might reuse.
- **Lock-free data structures at scale.** The `atomic(9)` family covers small coordination flags; it does not cover SMR, hazard pointers, RCU analogues, or full lock-free queues. Chapter 15 touches on atomic operations and epoch briefly; the deeper lock-free story belongs to a specialised kernel-internals discussion.
- **Detailed scheduler tuning.** Thread priorities, RT classes, priority inheritance, CPU affinity: out of scope. We pick sensible defaults and move on.
- **Userland POSIX semaphores and SysV IPC.** `sema(9)` in the kernel is a different beast. Chapter 15 focuses on the kernel primitive.
- **Performance micro-benchmarks.** Lockstat and DTrace lock profiling get a mention, not a full treatment. A dedicated performance chapter later in the book, when it exists, will carry that load.
- **Cross-process coordination primitives.** Some drivers need to coordinate with userland helpers; that problem is fundamentally different and belongs to a later chapter about ioctl-based protocols.

Staying inside those lines keeps the chapter's mental model coherent. Chapter 15 adds the coordination toolkit; Part 4 and later chapters apply the toolkit to hardware-facing scenarios.

### Estimated Time Investment

- **Reading only**: about three to four hours. The API surface is small but the composition takes some thought.
- **Reading plus typing the worked examples**: seven to nine hours over two sessions. The driver evolves in four stages.
- **Reading plus all labs and challenges**: twelve to sixteen hours over three or four sessions, including time to run stress tests against race-prone code paths.

If you find Section 5 (cross-subsystem coordination) disorienting on first pass, that is normal. The material is conceptually simple but requires holding several parts of the driver in your head at once. Stop, re-read the worked handshake in Section 5, and continue when the diagram has settled.

### Prerequisites

Before starting this chapter, confirm:

- Your driver source matches Chapter 14 Stage 4 (`stage4-final`). The starting point assumes every Chapter 14 primitive, every Chapter 13 callout, every Chapter 12 cv and sx, and the Chapter 11 concurrent-IO model.
- Your lab machine runs FreeBSD 14.3 with `/usr/src` on disk and matching the running kernel.
- A debug kernel with `INVARIANTS`, `WITNESS`, `WITNESS_SKIPSPIN`, `DDB`, `KDB`, and `KDB_UNATTENDED` is built, installed, and booting cleanly.
- You understand the Chapter 14 detach ordering well enough to extend it without getting lost.
- You have a comfortable mental model of `cv_wait_sig` and `cv_timedwait_sig` from Chapters 12 and 13.

If any of the above is shaky, fix it now rather than pushing through Chapter 15 and trying to reason from a moving foundation. The Chapter 15 primitives are sharper than what came before, and they amplify whatever discipline (or lack of it) the driver already has.

### How to Get the Most Out of This Chapter

Three habits will pay off quickly.

First, keep `/usr/src/sys/kern/kern_sema.c` and `/usr/src/sys/sys/sema.h` bookmarked. The implementation is short, under two hundred lines, and it is the shortest path to understanding what a FreeBSD semaphore actually does. Read `_sema_wait`, `_sema_post`, and `_sema_timedwait` carefully once. Knowing that a semaphore is "a counter plus a mutex plus a cv, wrapped in an API" makes the rest of the chapter feel obvious.

> **A note on line numbers.** Every pointer into the source in this chapter hangs on a function, macro, or structure name, not on a numeric line. `sema_init` and `_sema_wait` in `kern_sema.c`, and `sx_try_upgrade` in `/usr/src/sys/kern/kern_sx.c`, will remain findable by those names across FreeBSD 14.x point releases; the line each one occupies can drift as the surrounding code is revised. When in doubt, grep for the symbol.

Second, compare every new primitive to what you would have written with the old ones. The exercise "if I did not have sema(9), how would I express this?" is instructive. Writing the alternative with mtx and cv is usually possible, but the semaphore version is often half as long and substantially clearer. Seeing the contrast is how the value of the primitive becomes concrete.

Third, type the changes by hand and run each stage under `WITNESS`. Advanced synchronisation bugs are almost always detected by `WITNESS` on first contact if you run a debug kernel; they are almost always silent on a production kernel until the first crash. The companion source under `examples/part-03/ch15-more-synchronization/` is the reference version, but the muscle memory of typing `sema_init(&sc->writers_sema, 4, "myfirst writers")` once is worth more than reading it ten times.

### Roadmap Through the Chapter

The sections in order are:

1. **When mutexes and condition variables are not enough.** A survey of the shapes of problem that benefit from a different primitive.
2. **Using semaphores in the FreeBSD kernel.** The `sema(9)` API in depth, with the writer-cap refactor as Stage 1 of the Chapter 15 driver.
3. **Read-mostly scenarios and shared access.** Refined sx(9) patterns, including `sx_try_upgrade` and `sx_downgrade`, with a small statistics-cache refactor as Stage 2.
4. **Condition variables with timeout and interruption.** A careful treatment of `cv_timedwait_sig`, EINTR vs ERESTART, partial-progress handling, and the sysctl tuning that lets readers observe the behaviour. Stage 3 of the driver.
5. **Synchronisation between modules or subsystems.** Handshakes between callouts, tasks, and user threads via small state flags. Atomic operations and memory ordering at an introductory level. Stage 4 begins here.
6. **Synchronisation and modular design.** The `myfirst_sync.h` header, the naming discipline, and how the driver changes shape when synchronisation is encapsulated.
7. **Testing advanced synchronisation.** Stress kits, fault injection, and the observability sysctls that let you see the primitives working.
8. **Refactoring and versioning.** Stage 4 complete, version bump to `0.9-coordination`, `LOCKING.md` extended, and the Part 3 closing regression pass.

After the eight sections come hands-on labs, challenge exercises, a troubleshooting reference, a Wrapping Up that closes Part 3, and a bridge to Chapter 16 that opens Part 4. The same reference-and-cheat-sheet material that Chapters 13 and 14 ended with returns here at the end.

If this is your first pass, read linearly and do the labs in order. If you are revisiting, Sections 5 and 6 stand alone and make good single-sitting reads.



## Section 1: When Mutexes and Condition Variables Are Not Enough

The Chapter 11 mutex and the Chapter 12 condition variable are the default primitives of FreeBSD driver synchronisation. Nearly every driver uses them. Many drivers use nothing else. For a large class of problems the combination is exactly right: a mutex protects shared state, a condition variable lets a waiter sleep until the state matches a predicate, and the two together express "wait for state to become acceptable" and "tell others that state has changed" cleanly.

This section is about the problems where that default is awkward. Not because the mutex-and-cv combination cannot express the invariant, but because it expresses it more verbosely and more error-prone than a different primitive. Recognising those shapes is the first step toward using the right tool.

### The Shape of the Mismatch

Every synchronisation primitive has an underlying model of what it protects. A mutex protects mutual exclusion: at most one thread executes inside the lock at a time. A condition variable protects a predicate: a waiter sleeps until the predicate becomes true, and the signaller asserts that the predicate has changed. The two compose because the condition variable's wait drops and reacquires the mutex automatically, which lets the waiter observe the predicate under the lock, release the lock during the sleep, and regain the lock on wakeup.

The mismatch appears when the invariant you are protecting is not best described as "at most one" or "wait for a predicate". A handful of common shapes recur.

**Bounded admission.** The invariant is "at most N of a thing at once". For N equal to one, a mutex is natural. For N greater than one, the mutex-plus-cv-plus-counter version requires you to write an explicit counter, test it under the mutex, sleep on a cv if the counter is at N, decrement on entry, re-signal on exit, and rediscover the right wakeup policy. The semaphore primitive expresses the same invariant in three calls: `sema_init(&s, N, ...)`, `sema_wait(&s)` at entry, `sema_post(&s)` at exit.

**Read-mostly state with occasional promotion.** The invariant is "many readers concurrently, or one writer; when a reader detects a need to write, promote". The sx(9) lock handles the many-readers-or-one-writer part natively. The promotion part (`sx_try_upgrade`) is a primitive that a mutex-plus-cv version has to simulate with a rwlock-like counter and retry logic.

**Predicate that must survive signal interruption with partial-progress preservation.** A `read(2)` that copied half its requested bytes and is now sleeping for more should, on a signal, return the copied bytes rather than EINTR. `cv_timedwait_sig` gives you the EINTR and ERESTART distinction; writing the equivalent with raw `cv_wait` plus a periodic signal check is possible but error-prone.

**Cross-component shutdown coordination.** Several parts of the driver (callouts, tasks, user threads) need to observe "the driver is shutting down" consistently. A mutex-protected flag is an option. An atomic flag with a seq-cst fence on the writer and acquire loads on readers is often cheaper and clearer for this specific pattern, and the chapter will show when to pick which.

**Rate-limited retries.** "Do this at most once per 100 ms, skip if already in progress." Expressible with a mutex and a timer, but a taskqueue plus a timeout task plus an atomic test-and-set on an "already scheduled" flag is often cleaner. This pattern came up at the end of Chapter 14; Chapter 15 refines it.

For each shape, Chapter 15 picks a primitive that fits and shows the refactor side by side. The purpose is not to argue that the semaphore or the sx upgrade or the atomic flag is "better". The purpose is to let you pick the tool that matches the problem, so your driver reads cleanly to the next person who opens it.

### A Concrete Motivating Example: Too Many Writers

A motivating example to make the mismatch concrete. Suppose the driver wants to limit the number of concurrent writers. "Concurrent writers" means user threads that are simultaneously inside the `myfirst_write` handler past the initial validation. The limit is a small integer, say four, exposed as a sysctl tuning knob.

The mutex-plus-counter version looks like this:

```c
/* In the softc: */
int writers_active;
int writers_limit;   /* Configurable via sysctl. */
struct cv writer_cv;

/* In myfirst_write, at entry: */
MYFIRST_LOCK(sc);
while (sc->writers_active >= sc->writers_limit) {
        int error = cv_wait_sig(&sc->writer_cv, &sc->mtx);
        if (error != 0) {
                MYFIRST_UNLOCK(sc);
                return (error);
        }
        if (!sc->is_attached) {
                MYFIRST_UNLOCK(sc);
                return (ENXIO);
        }
}
sc->writers_active++;
MYFIRST_UNLOCK(sc);

/* At exit: */
MYFIRST_LOCK(sc);
sc->writers_active--;
cv_signal(&sc->writer_cv);
MYFIRST_UNLOCK(sc);
```

Every line is necessary. The loop handles spurious wakeups and signal returns. The signal check preserves partial progress (if any). The `is_attached` check ensures we do not proceed after detach. The cv_signal wakes the next waiter. The caller must remember to decrement.

The semaphore version looks like this:

```c
/* In the softc: */
struct sema writers_sema;

/* In attach: */
sema_init(&sc->writers_sema, 4, "myfirst writers");

/* In destroy: */
sema_destroy(&sc->writers_sema);

/* In myfirst_write, at entry: */
sema_wait(&sc->writers_sema);
if (!sc->is_attached) {
        sema_post(&sc->writers_sema);
        return (ENXIO);
}

/* At exit: */
sema_post(&sc->writers_sema);
```

Five lines of runtime logic, including the attached check. The primitive expresses the invariant directly. A reader who sees `sema_wait(&sc->writers_sema)` understands the intent in one glance.

Note what the semaphore version gives up. `sema_wait` is not signal-interruptible (as we will see in Section 2, FreeBSD's `sema_wait` uses `cv_wait` internally, not `cv_wait_sig`). If you need interruptibility, you fall back to the mutex-plus-cv version or combine `sema_trywait` with a separate interruptible wait. Every primitive has its trade-offs; Section 2 names them.

The broader point is that neither version is "wrong". The mutex-plus-counter version is correct and has been used in drivers for decades. The semaphore version is correct and clearer for this specific invariant. Knowing both lets you pick the right one for the specific constraints at hand.

### The Rest of the Section Previews the Chapter

Section 1 is deliberately short. The rest of the chapter unfolds each shape in its own section with its own refactor of the `myfirst` driver:

- Section 2 does the writer-cap semaphore refactor as Stage 1.
- Section 3 does the read-mostly sx refinement as Stage 2.
- Section 4 does the interruptible-wait refinement as Stage 3.
- Section 5 does the cross-component handshake as part of Stage 4.
- Section 6 extracts the synchronisation vocabulary into `myfirst_sync.h`.
- Section 7 writes the stress tests.
- Section 8 ties it all together and ships `0.9-coordination`.

Before diving in, one general observation. The Chapter 15 changes are small in lines of code. The whole chapter probably adds fewer than two hundred lines to the driver. What it adds in mental model is larger. Each primitive we introduce expresses an invariant that was implicit in the Chapter 14 driver; making it explicit is most of the value.

### Wrapping Up Section 1

A mutex and a condition variable cover most driver synchronisation. When the invariant is "at most N", "many readers or one writer with occasional promotion", "interruptible wait with partial progress", "cross-component shutdown", or "rate-limited retries", a different primitive expresses the intent more directly and leaves less room for bugs. Section 2 introduces the first of those primitives, the counting semaphore.



## Section 2: Using Semaphores in the FreeBSD Kernel

A counting semaphore is a small primitive. Internally it is a counter, a mutex, and a condition variable; the API wraps those three into operations that expose the counter-and-wait-for-positive semantics as its main interface. FreeBSD's kernel semaphore lives in `/usr/src/sys/sys/sema.h` and `/usr/src/sys/kern/kern_sema.c`. The whole implementation is under two hundred lines. Reading it once is the fastest way to understand what the API guarantees.

This section covers the API in depth, compares semaphores with mutexes and condition variables, walks through the writer-cap refactor as Stage 1 of the Chapter 15 driver, and names the trade-offs that come with the primitive.

### The Counting Semaphore, Precisely

A counting semaphore holds a non-negative integer. The API exposes two core operations:

- `sema_post(&s)` increments the counter. If anyone was waiting because the counter was zero, one of them is woken up.
- `sema_wait(&s)` decrements the counter if it is positive. If the counter is zero, the caller sleeps until `sema_post` increments it, then decrements and returns.

Those two operations, composed, give you bounded admission. Initialise the semaphore with N. Each participant calls `sema_wait` on entry and `sema_post` on exit. The invariant "at most N participants are between their wait and their post" is preserved automatically.

The FreeBSD counting semaphore differs from a binary semaphore (which can only be 0 or 1) in that the counter can go higher than 1. A binary semaphore is effectively a mutex, with one important difference: a semaphore has no ownership concept. Any thread may call `sema_post`; any thread may call `sema_wait`. A mutex, by contrast, must be released by the same thread that acquired it. This lack of ownership is important for exactly the use cases semaphores are best at: a producer that posts and a consumer that waits, which might be different threads.

### The Data Structure

The data structure, from `/usr/src/sys/sys/sema.h`:

```c
struct sema {
        struct mtx      sema_mtx;       /* General protection lock. */
        struct cv       sema_cv;        /* Waiters. */
        int             sema_waiters;   /* Number of waiters. */
        int             sema_value;     /* Semaphore value. */
};
```

Four fields. `sema_mtx` is the semaphore's own internal mutex. `sema_cv` is the condition variable that waiters block on. `sema_waiters` counts the number of currently-blocked waiters (for diagnostic purposes and to avoid unnecessary broadcasts). `sema_value` is the counter itself.

You never touch these fields directly. The API is the contract; the structure is shown here once so you can visualise what the primitive is.

### The API

From `/usr/src/sys/sys/sema.h`:

```c
void sema_init(struct sema *sema, int value, const char *description);
void sema_destroy(struct sema *sema);
void sema_post(struct sema *sema);
void sema_wait(struct sema *sema);
int  sema_timedwait(struct sema *sema, int timo);
int  sema_trywait(struct sema *sema);
int  sema_value(struct sema *sema);
```

**`sema_init`**: initialises the semaphore with the given initial value and human-readable description. The description is used by kernel tracing facilities. The value must be non-negative; `sema_init` asserts this with `KASSERT`.

**`sema_destroy`**: tears the semaphore down. You must ensure no waiters are present when you call `sema_destroy`; the implementation asserts this. Typically you guarantee it by design: the destroy happens in detach, after every path that could `sema_wait` has been quiesced.

**`sema_post`**: increments the counter. If there are waiters, wakes one of them. Always succeeds.

**`sema_wait`**: if the counter is positive, decrements it and returns. Otherwise sleeps on the internal cv until `sema_post` increments the counter, at which point it decrements and returns. **`sema_wait` is not signal-interruptible**; it uses `cv_wait` under the hood, not `cv_wait_sig`. A signal will not wake the waiter. If you need interruptibility, `sema_wait` is the wrong tool; use a mutex-plus-cv pattern directly.

**`sema_timedwait`**: same as `sema_wait` but bounded by `timo` ticks. Returns 0 on success (value was decremented), `EWOULDBLOCK` on timeout. Internally uses `cv_timedwait`, so also not signal-interruptible.

**`sema_trywait`**: non-blocking variant. Returns 1 if the value was successfully decremented, 0 if the value was already zero. Note the unusual convention: 1 means success, 0 means failure. Most FreeBSD kernel APIs return 0 on success; `sema_trywait` is an exception. Be careful when reading or writing code that uses it.

**`sema_value`**: returns the current counter value. Useful for diagnostics; not useful for making synchronisation decisions, because the value can change immediately after the call returns.

### What a Semaphore Is Not

Three properties the FreeBSD kernel semaphore does not have. Each is important.

**No priority inheritance.** The comment at the top of `/usr/src/sys/kern/kern_sema.c` is explicit:

> Priority propagation will not generally raise the priority of semaphore "owners" (a misnomer in the context of semaphores), so should not be relied upon in combination with semaphores.

If you are protecting a resource and a high-priority thread is waiting on a semaphore held by a low-priority thread, the low-priority thread does not inherit the high priority. This is a consequence of the no-ownership design: there is no "holder" to raise. For resources where priority inheritance matters, use a mutex or a `lockmgr(9)` lock instead.

**Not signal-interruptible.** `sema_wait` and `sema_timedwait` are not interrupted by signals. A `read(2)` or `write(2)` that blocks in `sema_wait` will not return EINTR or ERESTART when the user sends SIGINT. If your syscall needs to respond to signals, you cannot block in `sema_wait` unconditionally. The two usual workarounds: structure the wait as `sema_trywait` plus an interruptible sleep on a separate cv, or keep `sema_wait` but arrange for the producer (the code that `sema_post`s) to also post when shutdown is in progress.

**No ownership.** Any thread can post; any thread can wait. This is a feature, not a bug, for the producer-consumer shape where one thread signals completion and another waits for it. It is a surprise if you were expecting mutex-like ownership semantics.

Knowing what a primitive is not is as important as knowing what it is. The FreeBSD kernel semaphore is a small, focused tool. Use it where it fits; reach for different primitives where it does not.

### A Real-World Example: Hyper-V storvsc

Before the driver refactor, a short look at a real FreeBSD driver that uses `sema(9)` heavily. The Hyper-V storage driver lives at `/usr/src/sys/dev/hyperv/storvsc/hv_storvsc_drv_freebsd.c`. It uses per-request semaphores to block a thread waiting for hardware completion. The pattern:

```c
/* In the request submission path: */
sema_init(&request->synch_sema, 0, "stor_synch_sema");
/* ... send command to hypervisor ... */
sema_wait(&request->synch_sema);
/* At this point the completion handler has posted; work is done. */
sema_destroy(&request->synch_sema);
```

And in the completion callback (run from a different context):

```c
sema_post(&request->synch_sema);
```

The semaphore is initialised to zero, so `sema_wait` blocks. When hardware completes and the driver's completion handler runs, it posts, and the submitting thread unblocks. The ownership-free nature of the semaphore is exactly what makes this pattern work: a different thread (the completion handler) does the post than the one that does the wait.

The same driver uses a second semaphore (`hs_drain_sema`) for drain coordination during shutdown. The shutdown path waits on the semaphore; the request-completion path posts when all outstanding requests have finished.

These patterns are not inventions. They are the canonical uses of `sema(9)` in the FreeBSD tree. The Chapter 15 refactor uses a variation for the "at most N writers" invariant. The underlying idea is the same.

### The Writer-Cap Refactor: Stage 1

The first Chapter 15 change to the driver adds a counting semaphore that caps the number of concurrent `myfirst_write` callers. The cap is configurable via a sysctl, with a default of 4.

The change is not about performance. The driver can already handle many concurrent writers; the cbuf is protected by the mutex and writes serialise there anyway. The change is about expressing the invariant "at most N writers" as a first-class primitive. A real driver might use this pattern for more substantive reasons (a fixed-size DMA descriptor pool, a hardware command queue with bounded depth, a serial device with a transmit window); the refactor is a didactic vehicle for learning the primitive in a context you can run and observe.

### The Softc Addition

Add three members to `struct myfirst_softc`:

```c
struct sema     writers_sema;
int             writers_limit;              /* Current configured limit. */
int             writers_trywait_failures;   /* Diagnostic counter. */
```

`writers_sema` is the semaphore itself. `writers_limit` records the current configured value so the sysctl handler can detect changes. `writers_trywait_failures` counts the number of times a writer tried to enter, could not, and returned EAGAIN (for `O_NONBLOCK` opens) or EWOULDBLOCK (for bounded waits).

### Initialising and Destroying the Semaphore

In `myfirst_attach`, before any code that could call `sema_wait` (so typically alongside the other `sema_init`/`cv_init` calls early in attach):

```c
sema_init(&sc->writers_sema, 4, "myfirst writers");
sc->writers_limit = 4;
sc->writers_trywait_failures = 0;
```

The initial value of 4 matches the default limit. If we later raise the limit dynamically, we will adjust the semaphore's value to match; Section 2 shows how.

In `myfirst_detach`, after every path that could `sema_wait` has been quiesced (which, at Stage 1, means after `is_attached` is cleared and all user syscalls have returned or failed with ENXIO):

```c
sema_destroy(&sc->writers_sema);
```

A subtle point here, and a genuinely tricky one worth slowing down for. `sema_destroy` asserts that no waiters are present; more importantly, it then calls `mtx_destroy` on the semaphore's internal mutex and `cv_destroy` on its internal cv. If any thread is still executing inside any `sema_*` function, that thread may be about to re-acquire the internal mutex when `mtx_destroy` races ahead and frees it. That is a use-after-free, not just an assertion failure.

The naive cure "just post `writers_limit` slots to wake blocked waiters, then destroy" is *almost* correct but has a real race. A woken thread returns from `cv_wait` with the internal `sema_mtx` held, then needs to execute `sema_waiters--` and the final `mtx_unlock`. If the detach thread runs `sema_destroy` before the woken thread reaches its final unlock, the internal mutex is destroyed underneath it.

In practice that window is short (the woken thread typically runs within microseconds of `cv_signal`), but correctness means we cannot rely on "usually works". The cure is a small extension: track every thread that might currently be inside `sema_*` code and wait for that count to reach zero before calling `sema_destroy`.

We add `sc->writers_inflight`, an int the driver treats as atomic. The write path increments it before calling `sema_wait` and decrements it after calling the matching `sema_post`. The detach path, after posting the wake-up slots, waits for the counter to reach zero:

```c
/* In the write path, early: */
atomic_add_int(&sc->writers_inflight, 1);
if (!sc->is_attached) {
        atomic_subtract_int(&sc->writers_inflight, 1);
        return (ENXIO);
}
... sema_wait / work / sema_post ...
atomic_subtract_int(&sc->writers_inflight, 1);

/* In detach, after the posts: */
while (atomic_load_acq_int(&sc->writers_inflight) > 0)
        pause("myfwrd", 1);
sema_destroy(&sc->writers_sema);
```

Why this works: any thread that could possibly be using the sema's internal state has been counted. Detach waits until every counted thread has finished its final `sema_post`, which by the time the decrement fires has already returned from every `sema_*` function. No thread is still holding or about to acquire the internal mutex when `sema_destroy` runs.

The pattern is worth remembering because it is general: any external primitive whose destroy races with in-flight callers can be drained the same way. `sema(9)` is the immediate example; you will see variants of this counter in real drivers whenever a primitive without a built-in drain needs to be torn down cleanly.

The chapter's Stage 1 through Stage 4 drivers all implement this pattern. Section 6 encapsulates the logic in `myfirst_sync_writer_enter`/`myfirst_sync_writer_leave` so the call sites read naturally; the inflight bookkeeping hides in the wrapper.

### Using the Semaphore in the Write Path

Add `sema_wait`/`sema_post` around the body of `myfirst_write`:

```c
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

        /* Chapter 15: enforce the writer cap. */
        if (ioflag & IO_NDELAY) {
                if (!sema_trywait(&sc->writers_sema)) {
                        MYFIRST_LOCK(sc);
                        sc->writers_trywait_failures++;
                        MYFIRST_UNLOCK(sc);
                        return (EAGAIN);
                }
        } else {
                sema_wait(&sc->writers_sema);
        }
        if (!sc->is_attached) {
                sema_post(&sc->writers_sema);
                return (ENXIO);
        }

        nbefore = uio->uio_resid;
        while (uio->uio_resid > 0) {
                /* ... same body as Chapter 14 ... */
        }

        sema_post(&sc->writers_sema);
        return (0);
}
```

Several things to notice.

The `IO_NDELAY` (non-blocking) case uses `sema_trywait`, which returns 1 on success and 0 on failure. Note the inverted convention: `if (!sema_trywait(...))` means "if we failed to acquire". Beginners miss this regularly; read the return value with care every time.

On `sema_trywait` failure the non-blocking caller gets EAGAIN. A diagnostic counter is incremented under the mutex (brief mutex acquire/release, unrelated to the semaphore).

The blocking case uses `sema_wait`. It is not signal-interruptible, so a blocking `write(2)` waiting for the semaphore cannot be interrupted by SIGINT. This is an important property; users must know it. For the current driver the semaphore is rarely contended in practice (the default limit of 4 is generous), so the interruptibility concern is largely theoretical. If the limit were 1 and writers genuinely queued, you might want to revisit using a semaphore here and instead use an interruptible primitive. Section 4 returns to this trade-off.

After the wait returns we check `is_attached`. If detach happened while we were blocked, we must not proceed with the write; we post the semaphore (restoring the count) and return ENXIO.

The `sema_post` at the exit path runs on every successful path. A common mistake is to forget it on an early return (for example, if an intermediate validation fails). The usual discipline is to make the post unconditional via a cleanup pattern: acquire, then all subsequent exits go through one common cleanup.

### The Sysctl Handler for the Limit

Users of the driver may want to adjust the writer cap at runtime. The sysctl handler:

```c
static int
myfirst_sysctl_writers_limit(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        int new, old, error, delta;

        old = sc->writers_limit;
        new = old;
        error = sysctl_handle_int(oidp, &new, 0, req);
        if (error || req->newptr == NULL)
                return (error);
        if (new < 1 || new > 64)
                return (EINVAL);

        MYFIRST_LOCK(sc);
        delta = new - sc->writers_limit;
        sc->writers_limit = new;
        MYFIRST_UNLOCK(sc);

        if (delta > 0) {
                /* Raised the limit: post the extra slots. */
                int i;
                for (i = 0; i < delta; i++)
                        sema_post(&sc->writers_sema);
        }
        /*
         * Lowering is best-effort: we cannot reclaim posted slots from
         * threads already in the write path. New entries will observe
         * the lower limit once the counter drains below the new cap.
         */
        return (0);
}
```

Interesting details.

Raising the limit requires posting extra slots to the semaphore. If the old limit was 4 and the new limit is 6, we need to post twice so two more writers can enter simultaneously.

Lowering the limit is harder. A semaphore has no way to "consume" excess slots back out of it. If the current counter is 4 and we want a limit of 2, we cannot reduce the counter except by waiting for writers to enter and not posting on their exit. That is complicated and rarely worth the code. Instead, the simple approach: lower the `writers_limit` field, and let the semaphore drain naturally to the new level as writers enter without replacement. The sysctl handler comment documents this behaviour.

The mutex is held only for the `writers_limit` read/write, not for the sema_post loop. Taking the mutex around `sema_post` would be incorrect anyway: `sema_post` acquires its own internal mutex, and we would be introducing a lock order `sc->mtx -> sc->writers_sema.sema_mtx` that nothing else uses. Since `writers_limit` is the only field we actually protect, the mutex window is small.

### Observing the Effect

With Stage 1 loaded, a few experiments.

Start many concurrent writers using a small shell loop:

```text
# for i in 1 2 3 4 5 6 7 8; do
    (yes "writer-$i" | dd of=/dev/myfirst bs=512 count=100 2>/dev/null) &
done
```

Eight writers start simultaneously. With `writers_limit=4` (the default), four enter the write loop and the other four block in `sema_wait`. As one finishes and calls `sema_post`, a blocked one wakes up. The throughput is slightly lower than unconstrained (because only four writers actively progress at any moment), but the cbuf never has more than four writers contending for the mutex.

Observe the semaphore value live:

```text
# sysctl dev.myfirst.0.stats.writers_sema_value
dev.myfirst.0.stats.writers_sema_value: 0
```

During the stress test the value should be near zero. When no writers are present it should equal `writers_limit`.

Tune the limit dynamically:

```text
# sysctl dev.myfirst.0.writers_limit=2
```

Rerun the eight-writer stress. Two writers progress; six block. Throughput drops accordingly.

Tune the limit back up:

```text
# sysctl dev.myfirst.0.writers_limit=8
```

All eight writers proceed concurrently.

Check the trywait-failure counter by using non-blocking writers (via `open` with `O_NONBLOCK`):

```text
# ./nonblock_writer_stress.sh
# sysctl dev.myfirst.0.stats.writers_trywait_failures
```

The count grows whenever a non-blocking writer is turned away because the semaphore was at zero.

### Common Mistakes

A short list of mistakes beginners make with `sema(9)`. Each has bitten real drivers; each has a simple rule.

**Forgetting to `sema_post` on an error path.** If the write path has a `return (error)` that bypasses the `sema_post`, the semaphore leaks a slot. After enough leaks the semaphore is permanently at zero and all writers block. The fix is either to place the `sema_post` in a single cleanup block that all exits flow through, or to audit every return statement to confirm it posts.

**`sema_wait` in a context that cannot sleep.** `sema_wait` blocks. It cannot be called from a callout callback, an interrupt filter, or any other non-sleeping context. The `WITNESS` assertion catches this on a debug kernel; the production kernel may deadlock or panic silently.

**Destroying a semaphore with waiters.** `sema_destroy` asserts that no waiters exist. In a driver's detach, the careful thing to do is drain every path that could wait before destroying. If the detach ordering is wrong (destroying before the waiters have woken), the assertion fires on a debug kernel and the destroy silently corrupts on a production kernel.

**Using `sema_wait` where signal interruptibility is needed.** Users expect `read(2)` and `write(2)` to respond to SIGINT. If the syscall blocks in `sema_wait`, it does not. Either pick a different primitive or structure the code so `sema_wait` is short enough that signal latency is acceptable.

**Confusing `sema_trywait`'s return value.** Returns 1 on success, 0 on failure. Most FreeBSD kernel APIs return 0 on success. A misreading of the return value produces the opposite of the intended behaviour. Always double-check this one.

**Assuming priority inheritance.** If the invariant requires that a high-priority waiter raise the effective priority of the thread that will post, `sema(9)` will not do that. Use a mutex or a `lockmgr(9)` lock instead.

### A Note on When Not to Use Semaphores

For completeness, a short list of situations where `sema(9)` is the wrong tool.

- **When the invariant is "exclusive ownership of a resource".** That is a mutex. A semaphore initialised to 1 approximates it but loses ownership semantics and priority inheritance.
- **When the waiter must be signal-interruptible.** Use `cv_wait_sig` or `cv_timedwait_sig` with your own counter.
- **When the work is short and contention is high.** The semaphore's internal mutex is a single point of serialisation. For very short critical sections, the overhead may dominate.
- **When priority inheritance is required.** Use a mutex or `lockmgr(9)`.
- **When you need more than counting.** If the invariant is "wait until this specific complex predicate holds", a mutex and a cv that tests the predicate is the right tool.

For the driver's writer-cap use case, none of these disqualifications apply. The semaphore is the right tool, the refactor is small, and the resulting code is readable. Stage 1 of the Chapter 15 driver keeps the new vocabulary and moves on.

### Wrapping Up Section 2

A counting semaphore is a counter, a mutex, and a condition variable wrapped into a small API. `sema_init`, `sema_wait`, `sema_post`, `sema_trywait`, `sema_timedwait`, `sema_value`, and `sema_destroy` cover the whole surface. The primitive is ideal for bounded admission and for producer-consumer completion shapes where the producer and consumer are different threads. It lacks priority inheritance, signal interruptibility, and ownership, and those limitations are real. Stage 1 of the Chapter 15 driver applied a writer-cap semaphore; the next section applies a read-mostly sx refinement.



## Section 3: Read-Mostly Scenarios and Shared Access

The sx lock from Chapter 12 is already in the driver. `sc->cfg_sx` protects the `myfirst_config` structure, and the configuration sysctls acquire it in shared mode for reads and exclusive mode for writes. That pattern is correct and, for the configuration use case, sufficient. This section refines the sx pattern to cover a slightly different shape: a read-mostly cache where readers occasionally notice that the cache needs updating and must promote to a writer briefly.

This section also introduces a small number of sx operations the driver has not used yet: `sx_try_upgrade`, `sx_downgrade`, `sx_xlocked`, and a few introspection macros. The Stage 2 driver refactor adds a tiny statistics cache protected by its own sx and uses the upgrade pattern to refresh the cache under light contention.

### The Read-Mostly Cache Problem

A concrete motivating problem for the Stage 2 refactor. Suppose the driver wants to expose a computed statistic, "average bytes written per second over the last 10 seconds". The statistic is expensive to compute (it requires a walk over a per-second history buffer) and is read often (every sysctl read, every heartbeat log line). A naive implementation recomputes on every read. A better implementation caches the result and invalidates the cache periodically.

The cache has three properties:

1. Reads vastly outnumber writes. Any number of threads may read simultaneously; only the occasional cache refresh needs to write.
2. Readers sometimes detect that the cache is stale. When that happens, the reader wants to promote to a writer briefly, refresh the cache, and return to reading.
3. Refreshing the cache takes a few microseconds. A reader that promotes and refreshes still wants to release the exclusive lock quickly.

The sx lock handles properties 1 and 3 natively: many readers can hold `sx_slock` simultaneously; a writer with `sx_xlock` excludes readers. Property 2 requires `sx_try_upgrade`.

### `sx_try_upgrade` and `sx_downgrade`

Two operations on the sx lock that Chapter 12 did not introduce.

`sx_try_upgrade(&sx)` attempts to atomically promote a shared lock to an exclusive lock. Returns non-zero on success, zero on failure. A failure means another thread also holds the shared lock (exclusive-with-other-readers is not representable; the upgrade can only succeed if the calling thread is the sole shared holder). On success the shared lock is gone and the caller now holds the exclusive lock.

`sx_downgrade(&sx)` atomically demotes an exclusive lock to a shared lock. Always succeeds. The exclusive holder becomes a shared holder; other shared lockers can then join.

The pattern for read-with-occasional-upgrade:

```c
sx_slock(&sx);
if (cache_stale(&cache)) {
        if (sx_try_upgrade(&sx)) {
                /* Promoted to exclusive. */
                refresh_cache(&cache);
                sx_downgrade(&sx);
        } else {
                /*
                 * Upgrade failed: another reader holds the lock.
                 * Release the shared lock, take the exclusive lock,
                 * refresh, downgrade.
                 */
                sx_sunlock(&sx);
                sx_xlock(&sx);
                if (cache_stale(&cache))
                        refresh_cache(&cache);
                sx_downgrade(&sx);
        }
}
use_cache(&cache);
sx_sunlock(&sx);
```

Three things to notice.

The happy path is the `sx_try_upgrade` success. The upgrade is atomic: at no point is the lock released and re-acquired, so no other writer can slip in between. For a read-mostly workload where readers rarely contend with each other, this path dominates.

The fallback path when `sx_try_upgrade` fails drops the shared lock entirely, acquires the exclusive lock from scratch, and re-checks the stale predicate. The re-check is essential: between dropping the shared lock and acquiring the exclusive lock, another thread may have refreshed the cache. Without the re-check you would refresh redundantly.

The final `sx_sunlock` after `sx_downgrade` is always correct because the downgraded state is shared.

This pattern is surprisingly common in the FreeBSD source tree. Search for `sx_try_upgrade` under `/usr/src/sys/` and you find it in several subsystems, including VFS and the routing table updates.

### A Worked Application: Stage 2 Driver

Stage 2 of the Chapter 15 driver adds a small statistics cache protected by its own sx lock. The cache holds a single integer, "bytes_written in the last 10 seconds, as of the last refresh", and a timestamp recording when the cache was last refreshed.

The softc addition:

```c
struct sx       stats_cache_sx;
uint64_t        stats_cache_bytes_10s;
uint64_t        stats_cache_last_refresh_ticks;
```

The cache validity is based on a timestamp. If the current `ticks` differs from `stats_cache_last_refresh_ticks` by more than `hz` (one second's worth), the cache is considered stale. Any sysctl read of the cached value triggers a staleness check; if stale, the reader promotes and refreshes.

### The Cache Refresh Function

The refresh function is trivial for the didactic version: it just reads the current counter and records the current time.

```c
static void
myfirst_stats_cache_refresh(struct myfirst_softc *sc)
{
        KASSERT(sx_xlocked(&sc->stats_cache_sx),
            ("stats cache not exclusively locked"));
        sc->stats_cache_bytes_10s = counter_u64_fetch(sc->bytes_written);
        sc->stats_cache_last_refresh_ticks = ticks;
}
```

The `KASSERT` documents the contract: this function must be called with the sx held exclusively. A debug kernel catches violations at runtime.

### The Sysctl Handler

The sysctl handler that reads the cached value:

```c
static int
myfirst_sysctl_stats_cached(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        uint64_t value;
        int stale;

        sx_slock(&sc->stats_cache_sx);
        stale = (ticks - sc->stats_cache_last_refresh_ticks) > hz;
        if (stale) {
                if (sx_try_upgrade(&sc->stats_cache_sx)) {
                        myfirst_stats_cache_refresh(sc);
                        sx_downgrade(&sc->stats_cache_sx);
                } else {
                        sx_sunlock(&sc->stats_cache_sx);
                        sx_xlock(&sc->stats_cache_sx);
                        if ((ticks - sc->stats_cache_last_refresh_ticks) > hz)
                                myfirst_stats_cache_refresh(sc);
                        sx_downgrade(&sc->stats_cache_sx);
                }
        }
        value = sc->stats_cache_bytes_10s;
        sx_sunlock(&sc->stats_cache_sx);

        return (sysctl_handle_64(oidp, &value, 0, req));
}
```

The shape matches the pattern from the previous subsection. Worth reading once carefully. The staleness check happens twice in the fallback path: once to decide whether to take the exclusive lock at all, and once after acquisition to confirm the staleness still applies.

### Attach and Detach

Initialise the sx in `myfirst_attach`, alongside the existing `cfg_sx`:

```c
sx_init(&sc->stats_cache_sx, "myfirst stats cache");
sc->stats_cache_bytes_10s = 0;
sc->stats_cache_last_refresh_ticks = 0;
```

Destroy in `myfirst_detach`, after the taskqueue and mutex are torn down (the sx is ordered below the mutex in the lock graph; destroy it after the mutex for symmetry with init):

```c
sx_destroy(&sc->stats_cache_sx);
```

No waiters should be present at destroy time. Readers may still be in progress if detach races with a sysctl, but the detach path does not access the cache sx so the two do not directly conflict. If a sysctl is executing when detach proceeds, the sysctl framework holds its own reference and the context will tear down in order.

### Observing the Effect

Read the cached stat a thousand times quickly:

```text
# for i in $(jot 1000 1); do
    sysctl -n dev.myfirst.0.stats.bytes_written_10s >/dev/null
done
```

Most reads hit the cache without promoting. Only the first read after a cache expiry refreshes. The result: near-zero contention on the stats cache sx under read-mostly load.

Observe refresh rate via DTrace:

```text
# dtrace -n '
  fbt::myfirst_stats_cache_refresh:entry {
        @[execname] = count();
  }
' -c 'sleep 10'
```

Should show roughly ten refreshes per second (one per cache expiry), regardless of how many read requests arrived.

### The sx Macro Vocabulary

A few macros and helpers the chapter has not used yet that are worth knowing.

`sx_xlocked(&sx)` returns non-zero if the current thread holds the sx exclusively. Useful inside assertions. Does not tell you whether a different thread holds it; there is no equivalent query for that.

`sx_xholder(&sx)` returns the thread pointer of the exclusive holder, or NULL if no one holds it exclusively. Useful in debugging output.

`sx_assert(&sx, what)` asserts a property of the lock state. `SX_LOCKED`, `SX_SLOCKED`, `SX_XLOCKED`, `SX_UNLOCKED`, `SX_XLOCKED | SX_NOTRECURSED`, and others are valid. Panics on mismatch when `INVARIANTS` is enabled.

For the Chapter 15 refactor we use `sx_xlocked` in the cache refresh KASSERT. The other macros are available when you need them.

### Trade-offs and Cautions

Some trade-offs worth naming.

**Shared locks have overhead.** An sx in shared mode still takes a spin on the internal spin lock plus a couple of atomic operations. For extremely hot paths (tens of millions of operations per second), this can be measurable. `atomic(9)` with a seq-cst fence is sometimes cheaper. For the driver's workloads, sx is fine.

**Upgrade failures are a real possibility.** A workload with many concurrent readers will see `sx_try_upgrade` fail often. The fallback path (drop shared, acquire exclusive, re-check) does the right thing but has slightly higher latency. For true read-mostly workloads where upgrades are rare, the success path dominates.

**Sx locks can sleep.** Unlike a mutex, sx's slow path blocks. Do not call `sx_slock`, `sx_xlock`, `sx_try_upgrade`, or `sx_downgrade` from a context that cannot sleep (callouts initialised without a sleepable lock, interrupt filters, etc.). Chapter 13 explains that the older `CALLOUT_MPSAFE` flag is deprecated; the modern test is whether the callout was set up through `callout_init(, 0)` or `callout_init_mtx(, &mtx, 0)`.

**Lock order still matters.** Adding an sx to the driver means adding a new node to the lock graph. Every code path that holds multiple locks must respect a consistent order. The Chapter 15 driver's final lock order is `sc->mtx -> sc->cfg_sx -> sc->stats_cache_sx`; `WITNESS` enforces it.

### Wrapping Up Section 3

An sx lock covers many-readers-or-one-writer naturally. `sx_try_upgrade` and `sx_downgrade` extend that to read-mostly-with-promotion. The pattern with a happy-path upgrade and a fallback re-check is the canonical way to express "a reader noticed a need to write, briefly". Stage 2 of the driver added a small statistics cache with this pattern; Stage 3 will refine signal-interruptible waits.



## Section 4: Condition Variables with Timeout and Interruption

The `cv_wait_sig` and `cv_timedwait_sig` primitives are already in the driver. Chapter 12 introduced them; Chapter 13 refined them for the tick-source driver. This section takes the next step: it distinguishes the return values these primitives produce, shows how to handle EINTR and ERESTART correctly, and refactors the driver's read path to preserve partial progress across signal interruption. This is Stage 3 of the Chapter 15 driver.

Unlike the previous sections, this one does not introduce a new primitive. It introduces a discipline for using a primitive you already know.

### What the Return Values Mean

`cv_wait_sig`, `cv_timedwait_sig`, `mtx_sleep` with the `PCATCH` flag, and similar signal-aware waits can return several values:

- **0**: normal wakeup. The caller was woken by a matching `cv_signal` or `cv_broadcast`. Re-check the predicate; if true, proceed; if false, wait again.
- **EINTR**: interrupted by a signal that has a handler installed. The caller should abandon the wait, do any appropriate cleanup, and return EINTR to its own caller.
- **ERESTART**: interrupted by a signal whose handler specifies automatic restart. The kernel will re-invoke the syscall. The driver should return ERESTART to the syscall layer, which arranges for the restart.
- **EWOULDBLOCK**: only from timed waits. The timeout fired before any wakeup or signal arrived.

The distinction between EINTR and ERESTART matters because the driver returns these values back through the syscall path and userland handles them differently:

- If the syscall returns EINTR, userland's `read(2)` or `write(2)` returns -1 with errno set to EINTR. User code that did not install a SA_RESTART signal handler sees this explicitly.
- If the syscall returns ERESTART, the syscall machinery restarts the syscall transparently. Userland never sees the signal delivery at this level; the signal handler ran, but the read call continues.

The practical consequence: if your `cv_wait_sig` returns EINTR, the user will see an EINTR from their `read(2)` and any partial progress they might have expected must be explicit (the read returns the bytes copied before the signal, not an error, by convention). If it returns ERESTART, the restart happens and the read continues where the kernel saw fit.

### The Partial-Progress Convention

UNIX convention for `read(2)` and `write(2)`: if a signal arrives after some data has been transferred, the syscall returns the number of bytes transferred, not an error. If no data has been transferred, the syscall returns EINTR (or restarts, depending on signal disposition).

Translating to the driver: at the entry of the read path, record the initial `uio_resid`. When the blocking wait returns a signal error, compare the current `uio_resid` with the recorded one. If progress was made, return 0 (which the syscall layer translates to "return the number of bytes copied"). If no progress was made, return the signal error.

The Chapter 12 driver already implements this convention for `myfirst_read` via the `nbefore` local and the "return -1 to caller to indicate partial progress" trick. Chapter 15 refines the handling, makes it explicit, and extends it to the write path.

### The Refactored Read Path

The Chapter 14 Stage 4 read path has this shape:

```c
while (uio->uio_resid > 0) {
        MYFIRST_LOCK(sc);
        error = myfirst_wait_data(sc, ioflag, nbefore, uio);
        if (error != 0) {
                MYFIRST_UNLOCK(sc);
                return (error == -1 ? 0 : error);
        }
        ...
}
```

And `myfirst_wait_data` returns -1 to signal "partial progress; return 0 to the user". That convention is correct but cryptic. The Stage 3 refactor replaces the -1 magic value with a named sentinel and documents the convention in a comment:

```c
#define MYFIRST_WAIT_PARTIAL    (-1)    /* partial progress already made */

static int
myfirst_wait_data(struct myfirst_softc *sc, int ioflag, ssize_t nbefore,
    struct uio *uio)
{
        int error, timo;

        MYFIRST_ASSERT(sc);
        while (cbuf_used(&sc->cb) == 0) {
                if (uio->uio_resid != nbefore) {
                        /*
                         * Some bytes already delivered on earlier loop
                         * iterations. Do not block further; return
                         * "partial progress" so the caller returns 0
                         * to the syscall layer, which surfaces the
                         * partial byte count.
                         */
                        return (MYFIRST_WAIT_PARTIAL);
                }
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
                switch (error) {
                case 0:
                        break;
                case EWOULDBLOCK:
                        return (EAGAIN);
                case EINTR:
                case ERESTART:
                        if (uio->uio_resid != nbefore)
                                return (MYFIRST_WAIT_PARTIAL);
                        return (error);
                default:
                        return (error);
                }
                if (!sc->is_attached)
                        return (ENXIO);
        }
        return (0);
}
```

Several changes from the Chapter 14 version.

The magic -1 is now `MYFIRST_WAIT_PARTIAL`, with a comment explaining what it means.

The error handling after the cv wait is explicit about what each return value means. EWOULDBLOCK becomes EAGAIN (which is the conventional user-facing error for "try again later"). EINTR and ERESTART are checked for partial progress: if any bytes were delivered, we return the partial sentinel; if not, we propagate the signal error.

The `default` case handles any other error the cv wait might return. Currently the kernel's `cv_timedwait_sig` returns only the values listed above, but explicit handling of the unexpected case is a habit worth keeping.

### Handling at the Caller

In `myfirst_read` the handling of the sentinel becomes slightly clearer:

```c
while (uio->uio_resid > 0) {
        MYFIRST_LOCK(sc);
        error = myfirst_wait_data(sc, ioflag, nbefore, uio);
        if (error != 0) {
                MYFIRST_UNLOCK(sc);
                if (error == MYFIRST_WAIT_PARTIAL)
                        return (0);
                return (error);
        }
        ...
}
```

The reader can see at a glance what "partial progress" means. A maintainer adding a new early-exit reason later knows to check whether it should propagate to the user or be suppressed as partial.

### The Write Path Gets the Same Treatment

The Chapter 14 write path already implements partial-progress handling in `myfirst_wait_room`. Stage 3 applies the same refactor there: replace -1 with `MYFIRST_WAIT_PARTIAL`, make the error-handling switch explicit, and document the convention.

One small additional change for the write path. The write path's sema_wait from Section 2 is not signal-interruptible. Before the semaphore change, a blocked write would be interruptible via `cv_wait_sig` inside `myfirst_wait_room`. After adding the semaphore, a write that blocks on `sema_wait` (waiting for a writer slot) is not interruptible.

Is that acceptable? For most workloads yes, because the writer cap is typically not contended. For a workload where the cap is 1 and writers genuinely queue for long periods, users would expect SIGINT to work. The trade-off is an explicit trade-off; Section 5 will show how to make the wait interruptible by layering a signal-aware wait around `sema_trywait`.

For Stage 3, we accept the non-interruptible `sema_wait` for the default case and note the trade-off in a comment:

```c
/*
 * The writer-cap semaphore wait is not signal-interruptible. For a
 * workload where the cap is rarely contended this is acceptable. If
 * you set writers_limit=1 and create a real queue of writers, consider
 * the interruptible alternative in Section 5.
 */
sema_wait(&sc->writers_sema);
```

### The Interruptible Wait Pattern

For readers who want the fully interruptible version now: combine `sema_trywait` with a retry loop that uses a cv for interruptible sleep. The code is moderately verbose, which is why Chapter 15 defers it to an optional subsection.

```c
static int
myfirst_writer_enter_interruptible(struct myfirst_softc *sc)
{
        int error;

        MYFIRST_LOCK(sc);
        while (!sema_trywait(&sc->writers_sema)) {
                if (!sc->is_attached) {
                        MYFIRST_UNLOCK(sc);
                        return (ENXIO);
                }
                error = cv_wait_sig(&sc->writers_wakeup_cv, &sc->mtx);
                if (error != 0) {
                        MYFIRST_UNLOCK(sc);
                        return (error);
                }
        }
        MYFIRST_UNLOCK(sc);
        return (0);
}
```

This requires a second cv (`writers_wakeup_cv`) that the exit path signals after every `sema_post`:

```c
sema_post(&sc->writers_sema);
/* Wake one interruptible waiter so they can retry sema_trywait. */
cv_signal(&sc->writers_wakeup_cv);
```

The interruptible version preserves EINTR/ERESTART handling correctly. It is longer than the plain `sema_wait` version, and for most drivers the trade-off is not worth the extra code. But the pattern exists when it is needed.

### Common Mistakes

**Treating EWOULDBLOCK as a normal wakeup.** The timed wait returns EWOULDBLOCK when the timer fires. Treating this as 0 and re-testing the predicate is wrong: the predicate is probably still false and the loop spins indefinitely.

**Treating EINTR as a recoverable wakeup.** EINTR means the caller should abandon the wait. A loop that does `while (... != 0) cv_wait_sig(...)` with no EINTR handling never propagates signals back to user space.

**Forgetting the partial-progress check.** A read that copied half its bytes and gets interrupted should return the half; a naive implementation returns EINTR with zero bytes copied, losing the partial data.

**Mixing `cv_wait` with signal-interruptible callers.** `cv_wait` (without `_sig`) blocks even during signal delivery. A syscall that uses `cv_wait` cannot be interrupted; users' `SIGINT` does nothing until the predicate is satisfied. Always use `cv_wait_sig` in syscall contexts.

**Forgetting to re-check the predicate after wakeup.** Both signals and cv_signal wake the waiter. The predicate may not be true on wakeup (spurious wakeups are allowed by the API). Always check the predicate in a loop.

### Wrapping Up Section 4

Signal-interruptible waits have four distinct return values: 0 (normal), EINTR (signal without restart), ERESTART (signal with restart), EWOULDBLOCK (timeout). Each has a specific meaning and the driver must handle each explicitly. The partial-progress convention (return the bytes copied so far, not an error) is the UNIX standard for reads and writes. Stage 3 of the driver applied this discipline and made the partial-progress sentinel explicit. Section 5 takes the coordination story further.



## Section 5: Synchronization Between Modules or Subsystems

Up to now every primitive in the driver has been local to a single function or file section. A mutex protects a buffer; a cv signals readers; a sema limits writers; an sx caches statistics. Each primitive solves one problem in one place.

Real drivers have coordination that spans across subsystems. A callout fires, needs a task to finish its work, needs a user thread to see the resulting state, and needs another subsystem to notice that shutdown is in progress. The primitives you already know are sufficient; the difficulty is in composing them so the cross-component handshake is explicit and maintainable.

This section teaches the composition. It introduces a small atomic-flag discipline for cross-context shutdown visibility, a state-flag handshake between the callout and the task, and the beginnings of a wrapper layer that Section 6 will formalise. Stage 4 of the Chapter 15 driver begins here.

### The Shutdown-Flag Problem

A recurring problem in driver detach: several contexts need to know that shutdown is in progress. The Chapter 14 driver uses `sc->is_attached` as this flag, read under the mutex in most places and occasionally read unprotected (with the comment "read at handler entry may be unprotected"). This works, but it has two subtle issues.

First, the unprotected read is technically undefined behaviour in pure C. A concurrent writer and an unsynchronised reader is a data race; the compiler is free to transform the code in ways that assume no concurrent access. Current kernel compilers rarely do, but the code is not strictly portable and a future compiler could break it.

Second, the mutex-protected reads serialise across a lock even when the only thing you want is a quick "is it shutdown yet" peek. In a hot path this cost is measurable.

The modern discipline: use `atomic_load_int` for reads and `atomic_store_int` (or `atomic_store_rel_int`) for writes. These operations are defined by the C memory model to be well-ordered and race-free. They are also very cheap: on x86 a plain load or store with the right barriers; on other architectures a single atomic instruction.

### The Atomic API in One Page

`/usr/src/sys/sys/atomic_common.h` and architecture-specific headers define the atomic operations. The ones you will use most:

- `atomic_load_int(p)`: reads `*p` atomically. No memory barrier.
- `atomic_load_acq_int(p)`: reads `*p` atomically with acquire semantics. Subsequent memory accesses cannot be reordered before the load.
- `atomic_store_int(p, v)`: writes `v` to `*p` atomically. No memory barrier.
- `atomic_store_rel_int(p, v)`: writes `v` to `*p` atomically with release semantics. Preceding memory accesses cannot be reordered after the store.
- `atomic_fetchadd_int(p, v)`: returns old `*p` and sets `*p = *p + v` atomically.
- `atomic_cmpset_int(p, old, new)`: if `*p == old`, sets `*p = new` and returns 1; otherwise returns 0.

For the shutdown flag the pattern is:

- Writer (detach): `atomic_store_rel_int(&sc->is_attached, 0)`. The release ensures any prior state changes (draining, cv broadcasts) are visible before the flag becomes 0.
- Readers (any context): `if (atomic_load_acq_int(&sc->is_attached) == 0) { ... }`. The acquire ensures any subsequent checks see the state the writer intended.

The chapter uses `atomic_load_acq_int` in read-side shutdown checks and `atomic_store_rel_int` in the detach path. This makes the shutdown visibility correct across every context without introducing a mutex cost in the hot path.

### Why Not Just a Mutex-Protected Flag?

A fair question. The answer is "because the atomic pattern is cheaper and equally correct for this specific invariant". The flag has exactly two states (1 and 0), the transition is one-way (from 1 to 0, then never back to 1 in this lifetime), and no reader needs atomicity-with-other-state-changes; each reader only wants "is it still attached?".

For an invariant with multiple fields or bidirectional transitions a mutex is the right tool. For a monotonic one-bit flag, atomics win.

### Applying the Atomic Flag

The Stage 4 refactor converts `sc->is_attached` reads to atomic loads where they currently happen outside the mutex. The places to change are:

- `myfirst_open`: the entry check `if (sc == NULL || !sc->is_attached)`.
- `myfirst_read`: the entry check after `devfs_get_cdevpriv`.
- `myfirst_write`: the entry check after `devfs_get_cdevpriv`.
- `myfirst_poll`: the entry check.
- Every callout callback: `if (!sc->is_attached) return;`.
- Every task callback's equivalent check (if any).
- `myfirst_tick_source` after acquiring the mutex (this one is under the mutex; it could be an atomic load but does not have to be).

The mutex-held checks inside `myfirst_wait_data`, `myfirst_wait_room`, and the blocking re-checks after cv wakeup stay as-is: they are already serialised by the mutex.

The detach write becomes:

```c
MYFIRST_LOCK(sc);
if (sc->active_fhs > 0) {
        MYFIRST_UNLOCK(sc);
        return (EBUSY);
}
atomic_store_rel_int(&sc->is_attached, 0);
cv_broadcast(&sc->data_cv);
cv_broadcast(&sc->room_cv);
MYFIRST_UNLOCK(sc);
```

The store-release pairs with the atomic-load-acquire reads in the other contexts. Any state change that happened before the store (for example, any prior shutdown preparation) is visible to any thread that later does an acquire-read.

The handler entry checks become:

```c
if (sc == NULL || atomic_load_acq_int(&sc->is_attached) == 0)
        return (ENXIO);
```

For the callout callbacks the check was under the mutex; we leave it under the mutex for consistency with the rest of the callback's serialisation. Some drivers convert even the callout check to an atomic read for performance; the Chapter 15 driver does not, because the mutex cost is negligible at the callout firing rate.

### The Callout-to-Task Handshake

A different cross-component coordination problem. Suppose the watchdog callout detects a stall and wants to trigger a recovery action in a task. The callout cannot do the recovery itself (it might sleep, call user-space, etc.). The current driver solves this by enqueuing a task from the callout. What it does not solve is "do not enqueue the task if the previous recovery is still in progress".

A small state flag solves it. Add to the softc:

```c
int recovery_in_progress;   /* 0 or 1; protected by sc->mtx */
```

The callout:

```c
static void
myfirst_watchdog(void *arg)
{
        struct myfirst_softc *sc = arg;
        /* ... existing watchdog logic ... */

        if (stall_detected && !sc->recovery_in_progress) {
                sc->recovery_in_progress = 1;
                taskqueue_enqueue(sc->tq, &sc->recovery_task);
        }

        /* ... re-arm as before ... */
}
```

The task:

```c
static void
myfirst_recovery_task(void *arg, int pending)
{
        struct myfirst_softc *sc = arg;

        /* ... recovery work ... */

        MYFIRST_LOCK(sc);
        sc->recovery_in_progress = 0;
        MYFIRST_UNLOCK(sc);
}
```

The flag is protected by the mutex (both writes happen under the mutex; the read in the callout happens under the mutex because callouts hold the mutex via `callout_init_mtx`). The invariant "at most one recovery task at a time" is preserved. A watchdog firing during recovery sees the flag set and does not enqueue.

This is a minimal example, but the pattern generalises. Any time the driver needs to coordinate "do X only if Y is not already happening", a state flag protected by an appropriate lock is the right tool.

### The Stage 4 Softc

Pulling it together. Stage 4 adds these fields:

```c
/* Semaphore and its diagnostic fields (from Stage 1). */
struct sema     writers_sema;
int             writers_limit;
int             writers_trywait_failures;

/* Stats cache (from Stage 2). */
struct sx       stats_cache_sx;
uint64_t        stats_cache_bytes_10s;
uint64_t        stats_cache_last_refresh_ticks;

/* Recovery coordination (new in Stage 4). */
int             recovery_in_progress;
struct task     recovery_task;
int             recovery_task_runs;
```

All three fields form a coherent subsystem-coordination substrate. Section 6 encapsulates the vocabulary. Section 8 ships the final version.

### Memory Ordering in One Subsection

Memory ordering can feel abstract; a concrete summary helps.

On strongly-ordered architectures (x86, amd64), plain loads and stores of aligned int-sized values are atomic with respect to other aligned int-sized values. A plain `int flag = 0` write is visible to all other CPUs promptly. You rarely need barriers.

On weakly-ordered architectures (arm64, riscv, powerpc), the compiler and CPU are free to reorder loads and stores as long as the sequence would appear correct to a single thread. A plain write by one CPU may be delayed from being visible to another CPU, and reads on the other CPU may be reordered relative to each other.

The `atomic(9)` API papers over the difference. `atomic_store_rel_int` and `atomic_load_acq_int` produce the correct barriers on every architecture. You do not need to know which architecture is weak or strong; you use the API and the right thing happens.

For the Chapter 15 driver, using `atomic_store_rel_int` on the detach write and `atomic_load_acq_int` on the entry checks gives you a driver that works correctly on both x86 and arm64. If the driver ever ships on arm64 systems (and FreeBSD 14.3 supports arm64 well), the discipline pays off.

### Wrapping Up Section 5

Cross-component coordination in a driver uses the same primitives as local synchronisation, composed. The atomic API covers cheap shutdown flags with correct memory ordering. State flags protected by the appropriate lock coordinate "at most one" invariants across callouts, tasks, and user threads. Stage 4 of the Chapter 15 driver added both patterns. Section 6 takes the next step and encapsulates the synchronisation vocabulary in a dedicated header.



## Section 6: Synchronization and Modular Design

The driver now uses five kinds of synchronisation primitives: a mutex, two condition variables, two sx locks, a counting semaphore, and atomic operations. Each one appears in several places across the source. A maintainer reading the file for the first time must reconstruct the synchronisation strategy from the scattered call sites.

This section encapsulates the synchronisation vocabulary in a small header, `myfirst_sync.h`, that names every operation the driver performs. The header does not add new primitives; it gives the existing primitives readable names and documents their contracts in one place. Stage 4 of the Chapter 15 driver introduces the header and updates the main source to use it.

A note on status before we go further. The `myfirst_sync.h` wrapper is **a recommendation, not a FreeBSD convention**. Most drivers under `/usr/src/sys/dev` call `mtx_lock`, `sx_xlock`, `cv_wait`, and friends directly; they do not ship a private synchronisation header. If you skim the tree, you will not find a community expectation that every driver provides such a layer. What the FreeBSD community *does* expect is a clear, documented lock order and a `LOCKING.md`-style comment block that a reviewer can follow, and that expectation we satisfy in every chapter of Part 3. The wrapper header is a stylistic extension that has worked well for several medium-sized drivers inside and outside the tree; it is valuable for this book because it turns the synchronisation vocabulary into something you can name, audit, and change in one place. If your future driver does not need the extra readability, skipping the header and keeping the primitive calls in the source is a perfectly normal choice. The underlying discipline, lock order, drain on detach, explicit contracts, is what matters; the wrapper is one way to keep that discipline visible, not the only way.

### Why Encapsulate

Three concrete benefits.

**Readability.** A code path that reads `myfirst_sync_writer_enter(sc)` tells the reader exactly what the call does. The same code path written as `if (ioflag & IO_NDELAY) { if (!sema_trywait(&sc->writers_sema)) ...` is correct but tells the reader less.

**Changeability.** If the synchronisation strategy changes (say, the writer-cap semaphore is replaced by an interruptible cv-based wait from Section 4), the change happens in one place in the header. The call sites in `myfirst_write` do not change.

**Verifiability.** The header is the single place where the synchronisation contracts are documented. A code review can verify "does every enter have a matching leave?" by grepping the header. Without the header the review must walk every call site.

The cost of encapsulation is minimal. A header of 100 to 200 lines. A half-hour of refactoring. A slight layer of indirection that a modern compiler inlines away.

### The Shape of `myfirst_sync.h`

The header names every synchronisation operation. It does not define new structures; the structures stay in the softc. It provides inline functions or macros that wrap the primitives.

A sketch:

```c
#ifndef MYFIRST_SYNC_H
#define MYFIRST_SYNC_H

#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/sx.h>
#include <sys/sema.h>
#include <sys/condvar.h>

struct myfirst_softc;       /* Forward declaration. */

/* Data-path mutex operations. */
static __inline void    myfirst_sync_lock(struct myfirst_softc *sc);
static __inline void    myfirst_sync_unlock(struct myfirst_softc *sc);
static __inline void    myfirst_sync_assert_locked(struct myfirst_softc *sc);

/* Configuration sx operations. */
static __inline void    myfirst_sync_cfg_read_begin(struct myfirst_softc *sc);
static __inline void    myfirst_sync_cfg_read_end(struct myfirst_softc *sc);
static __inline void    myfirst_sync_cfg_write_begin(struct myfirst_softc *sc);
static __inline void    myfirst_sync_cfg_write_end(struct myfirst_softc *sc);

/* Writer-cap semaphore operations. */
static __inline int     myfirst_sync_writer_enter(struct myfirst_softc *sc,
                            int ioflag);
static __inline void    myfirst_sync_writer_leave(struct myfirst_softc *sc);

/* Stats cache sx operations. */
static __inline void    myfirst_sync_stats_cache_read_begin(
                            struct myfirst_softc *sc);
static __inline void    myfirst_sync_stats_cache_read_end(
                            struct myfirst_softc *sc);
static __inline int     myfirst_sync_stats_cache_try_promote(
                            struct myfirst_softc *sc);
static __inline void    myfirst_sync_stats_cache_downgrade(
                            struct myfirst_softc *sc);
static __inline void    myfirst_sync_stats_cache_write_begin(
                            struct myfirst_softc *sc);
static __inline void    myfirst_sync_stats_cache_write_end(
                            struct myfirst_softc *sc);

/* Attach-flag atomic operations. */
static __inline int     myfirst_sync_is_attached(struct myfirst_softc *sc);
static __inline void    myfirst_sync_mark_detaching(struct myfirst_softc *sc);

#endif /* MYFIRST_SYNC_H */
```

Each function wraps exactly one primitive call plus any convention the call site needs. For example, `myfirst_sync_writer_enter` takes the `ioflag` parameter and picks between `sema_trywait` (for `IO_NDELAY`) and `sema_wait`. The caller does not have to know the trywait-vs-wait logic; the header does.

### The Implementation

Each function is a simple inline wrapper. Example implementations (for the most interesting ones):

```c
static __inline void
myfirst_sync_lock(struct myfirst_softc *sc)
{
        mtx_lock(&sc->mtx);
}

static __inline void
myfirst_sync_unlock(struct myfirst_softc *sc)
{
        mtx_unlock(&sc->mtx);
}

static __inline void
myfirst_sync_assert_locked(struct myfirst_softc *sc)
{
        mtx_assert(&sc->mtx, MA_OWNED);
}

static __inline int
myfirst_sync_writer_enter(struct myfirst_softc *sc, int ioflag)
{
        if (ioflag & IO_NDELAY) {
                if (!sema_trywait(&sc->writers_sema)) {
                        mtx_lock(&sc->mtx);
                        sc->writers_trywait_failures++;
                        mtx_unlock(&sc->mtx);
                        return (EAGAIN);
                }
        } else {
                sema_wait(&sc->writers_sema);
        }
        if (!myfirst_sync_is_attached(sc)) {
                sema_post(&sc->writers_sema);
                return (ENXIO);
        }
        return (0);
}

static __inline void
myfirst_sync_writer_leave(struct myfirst_softc *sc)
{
        sema_post(&sc->writers_sema);
}

static __inline int
myfirst_sync_is_attached(struct myfirst_softc *sc)
{
        return (atomic_load_acq_int(&sc->is_attached));
}

static __inline void
myfirst_sync_mark_detaching(struct myfirst_softc *sc)
{
        atomic_store_rel_int(&sc->is_attached, 0);
}
```

The `writer_enter` wrapper is the most complex; everything else is a one-liner. A header of this shape produces zero runtime overhead (the compiler inlines every call) and adds substantial readability.

### How the Source Changes

Every `mtx_lock(&sc->mtx)` in the main source becomes `myfirst_sync_lock(sc)`. Every `sema_wait(&sc->writers_sema)` becomes `myfirst_sync_writer_enter(sc, ioflag)` or a variant. Every `atomic_load_acq_int(&sc->is_attached)` becomes `myfirst_sync_is_attached(sc)`.

The main source reads more clearly:

```c
/* Before: */
if (ioflag & IO_NDELAY) {
        if (!sema_trywait(&sc->writers_sema)) {
                MYFIRST_LOCK(sc);
                sc->writers_trywait_failures++;
                MYFIRST_UNLOCK(sc);
                return (EAGAIN);
        }
} else {
        sema_wait(&sc->writers_sema);
}
if (!sc->is_attached) {
        sema_post(&sc->writers_sema);
        return (ENXIO);
}

/* After: */
error = myfirst_sync_writer_enter(sc, ioflag);
if (error != 0)
        return (error);
```

Five lines of intent become one. A reader who wants to know what `myfirst_sync_writer_enter` does opens the header and reads the implementation. A reader who accepts the interface reads on.

### Naming Conventions

A short discipline for picking names in a synchronisation wrapper layer.

**Name operations, not primitives.** `myfirst_sync_writer_enter` describes what the caller is doing (entering the writer section). `myfirst_sync_sema_wait` would describe the primitive (calling sema_wait), which is less useful.

**Use enter/leave pairs for scoped acquisition.** Every `enter` has a matching `leave`. This makes it visually obvious whether the driver always releases what it acquires.

**Use read/write pairs for shared/exclusive access.** `cfg_read_begin`/`cfg_read_end` for shared; `cfg_write_begin`/`cfg_write_end` for exclusive. The begin/end suffix mirrors the call-site structure.

**Use `is_` for predicates that return bool-like values.** `myfirst_sync_is_attached` reads like English.

**Use `mark_` for atomic state transitions.** `myfirst_sync_mark_detaching` describes the transition.

### What Not to Put in the Header

The header should wrap synchronisation primitives, not business logic. A function that acquires a lock and also does "interesting" work should stay in the main source; only the pure lock manipulation belongs in the header.

The header also should not hide important details. For example, `myfirst_sync_writer_enter` returns `EAGAIN` or `ENXIO` or 0; the caller must check. A header that silently "returned" on `ENXIO` would hide an important error path. The wrapper's contract must be explicit.

### A Related Discipline: Assertions

The header is a good place to put the assertions that document invariants. A function that must be called under the mutex can call `myfirst_sync_assert_locked(sc)` at entry:

```c
static void
myfirst_some_helper(struct myfirst_softc *sc)
{
        myfirst_sync_assert_locked(sc);
        /* ... */
}
```

On a debug kernel (with `INVARIANTS`) the assertion fires if the helper is ever called without the mutex. On a production kernel the assertion is elided.

The Chapter 14 code uses `MYFIRST_ASSERT`; the Chapter 15 refactor keeps this as `myfirst_sync_assert_locked` with the same behaviour.

### A Short WITNESS Walkthrough: Sleeping Under a Non-Sleepable Lock

Chapter 34 walks through a lock-order reversal between two mutexes. A separate class of WITNESS warning, equally common and just as cheap to prevent, deserves a short mention here because it lands squarely in the middle of Chapter 15's territory: the interaction between mutexes and sx locks.

Imagine a first-attempt refactor of the configuration read path. The author has just added a new `sx_slock` over the config blob for read-mostly access, and, without thinking, calls it from inside a code path that still holds the data-path mutex:

```c
static int
myfirst_read(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct myfirst_softc *sc = dev->si_drv1;
        int error;

        MYFIRST_LOCK(sc);                /* mtx_lock: non-sleepable */
        sx_slock(&sc->cfg_sx);           /* sx_slock: sleepable */
        error = myfirst_copy_out_locked(sc, uio);
        sx_sunlock(&sc->cfg_sx);
        MYFIRST_UNLOCK(sc);
        return (error);
}
```

The code compiles and, in light testing on a non-debug kernel, appears to run correctly. Load it on a kernel built with `options WITNESS` and run the same lab, and the console reports something close to this:

```text
lock order reversal: (sleepable after non-sleepable)
 1st 0xfffff800...  myfirst_sc_mtx (mutex) @ /usr/src/sys/modules/myfirst/myfirst.c:...
 2nd 0xfffff800...  myfirst_cfg_sx (sx) @ /usr/src/sys/modules/myfirst/myfirst.c:...
stack backtrace:
 #0 witness_checkorder+0x...
 #1 _sx_slock+0x...
 #2 myfirst_read+0x...
```

Two things are worth reading carefully in this report. The parenthetical "(sleepable after non-sleepable)" tells you exactly what the reversal is: the thread took a non-sleepable lock first (the mutex) and then asked for a sleepable one (the sx lock). WITNESS rejects this because `sx_slock` can sleep, and sleeping while a non-sleepable lock is held is a defined kernel bug class: the scheduler cannot take the thread off-CPU without also migrating the mutex's waiters, and the invariants that make `MTX_DEF` cheap stop holding. The second thing is that WITNESS reports it the very first time the path runs, long before any real contention. You do not have to reproduce a race; the warning fires on the ordering itself.

The fix is ordering discipline, not a different primitive. Take the sx lock first, then the mutex:

```c
sx_slock(&sc->cfg_sx);
MYFIRST_LOCK(sc);
error = myfirst_copy_out_locked(sc, uio);
MYFIRST_UNLOCK(sc);
sx_sunlock(&sc->cfg_sx);
```

Or, better for most drivers, read the config under the sx lock into a local snapshot and release the sx lock before touching the data-path mutex at all. The encapsulation in `myfirst_sync.h` helps here because the lock-order contract is named and documented in one place; a review that sees `myfirst_sync_cfg_slock` followed by `myfirst_sync_lock` can confirm the ordering at a glance.

This walkthrough deliberately stays shorter than Chapter 34's. The category of bug is different, and the lesson is specific to the sleepable/non-sleepable distinction Chapter 15 is built around. The broader lock-order reversal walkthrough belongs to the debugging chapter; this one belongs where the reader first composes sx locks and mutexes in the same code path.

### Wrapping Up Section 6

A small synchronisation header names every operation the driver performs and centralises the contracts. The main source reads more clearly; the header is the one place a maintainer looks to understand or change the strategy. Stage 4's `myfirst_sync.h` adds no new primitives; it encapsulates the ones from Sections 2 through 5. Section 7 writes the tests that validate the whole composition.



## Section 7: Testing Advanced Synchronization

Every primitive this chapter introduced has a failure mode. A semaphore with a missing `sema_post` leaks slots. An sx upgrade that does not re-check the predicate refreshes redundantly. A signal-interruptible wait that ignores EINTR deadlocks the caller. A state flag read without the right lock or atomic discipline silently reads stale values.

This section is about writing tests that surface those failure modes before users find them. The tests are not unit tests in the pure sense; they are stress harnesses that exercise the driver under concurrent load and check invariants. The Chapter 15 companion source includes three test programs; this section walks through each.

### Why Stress Tests Matter

Synchronisation bugs rarely show up in single-threaded testing. A forgotten `sema_post` is invisible until enough writers have passed through that the semaphore runs out. A misplaced atomic read is invisible until a specific interleaving occurs. A detach race is invisible until detach and unload happen with real concurrency.

Stress tests find these bugs by running the driver in configurations that expose the interleavings. Many concurrent readers and writers. Fast tick source. Frequent detach/reload cycles. Simultaneous sysctl writes. The harder the driver works, the more likely a latent bug is to surface.

The tests do not replace `WITNESS` or `INVARIANTS`. `WITNESS` catches lock-order violations at any load. `INVARIANTS` catches structural violations. The stress tests catch logic errors that neither static nor lightweight dynamic checks can detect.

### Test 1: Writer-Cap Correctness

The writer-cap semaphore's invariant is "at most `writers_limit` writers in `myfirst_write` at once". A test program starts many concurrent writers, each of which writes a few bytes, records its process ID in a tiny marker at the start of its writes, and proceeds. A monitor process reads the cbuf in the background and counts concurrent markers.

The test is in `examples/part-03/ch15-more-synchronization/tests/writer_cap_test.c`:

```c
/*
 * writer_cap_test: start N writers and verify no more than
 * writers_limit are simultaneously inside the write path.
 */
#include <sys/param.h>
#include <sys/time.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysctl.h>
#include <unistd.h>

#define N_WRITERS 16

int
main(int argc, char **argv)
{
        int fd, i;
        char buf[64];
        int writers = (argc > 1) ? atoi(argv[1]) : N_WRITERS;

        for (i = 0; i < writers; i++) {
                if (fork() == 0) {
                        fd = open("/dev/myfirst", O_WRONLY);
                        if (fd < 0)
                                err(1, "open");
                        snprintf(buf, sizeof(buf), "w%d\n", i);
                        for (int j = 0; j < 100; j++) {
                                write(fd, buf, strlen(buf));
                                usleep(1000);
                        }
                        close(fd);
                        _exit(0);
                }
        }
        while (wait(NULL) > 0)
                ;
        return (0);
}
```

The test fires `N_WRITERS` processes, each of which writes 100 short messages with a 1 ms delay. A reader process reads `/dev/myfirst` and observes interleaving.

A simple invariant check: the reader reads 100 bytes at a time and records how many distinct writer prefixes appear in that window. If `writers_limit` is 4, the reader should see at most 4 prefixes in any 100-byte window (plus or minus). More than 4 indicates the cap is not being enforced.

A more rigorous check uses `sysctl dev.myfirst.0.stats.writers_trywait_failures` to observe the failure rate in the `O_NONBLOCK` mode. If you set `writers_limit=2` and run 16 non-blocking writers, most of them should see EAGAIN; the failure count should grow rapidly.

### Test 2: Stats Cache Concurrency

The stats cache invariant is "many readers concurrently, refreshed at most once per second". A test:

- Start 32 concurrent reader processes, each reading `dev.myfirst.0.stats.bytes_written_10s` in a tight loop.
- Start 1 writer that keeps writing to the device.
- Watch the cache refresh rate via DTrace:

```text
# dtrace -n '
  fbt::myfirst_stats_cache_refresh:entry {
        @["refreshes"] = count();
  }
  tick-10s { printa(@); exit(0); }
'
```

Expected: approximately 10 refreshes per 10 seconds (one per stale expiry). Not 32 per 10 seconds; the cache eliminates the per-reader recomputation.

If the refresh rate spikes under contention, the `sx_try_upgrade` fast path is failing too often and the fallback (drop-and-reacquire) is introducing races. The driver code should handle this correctly; if it does not, the test exposes the bug.

### Test 3: Detach Under Load

The detach invariant is "detach completes cleanly even when every Chapter 14 and 15 primitive is under load". A detach test:

```text
# ./stress_all.sh &
# STRESS_PID=$!
# sleep 5
# kldunload myfirst
# kill -TERM $STRESS_PID
```

Where `stress_all.sh` runs:

- Several concurrent writers.
- Several concurrent readers.
- The tick source enabled at 1 ms.
- The heartbeat enabled at 100 ms.
- The watchdog enabled at 1 s.
- Occasional bulk_writer_flood sysctl writes.
- Occasional writers_limit sysctl adjustments.
- Occasional stats-cache reads.

The detach should complete. If it hangs or panics, the ordering discipline has a bug. With the Chapter 14 and 15 code correctly ordered, the test should pass reliably.

### Observing With DTrace

DTrace is the scalpel for synchronisation debugging. A few useful one-liners:

**Count cv wakeups per cv name.**

```text
# dtrace -n 'fbt::cv_signal:entry { @[stringof(arg0)] = count(); }'
```

Interpreting the output requires knowing your cv names; the driver's are `"myfirst data"` and `"myfirst room"`.

**Count semaphore operations.**

```text
# dtrace -n '
  fbt::_sema_wait:entry { @[probefunc] = count(); }
  fbt::_sema_post:entry { @[probefunc] = count(); }
'
```

In a balanced driver the post count equals the wait count over a long run (plus the initial value from `sema_init`).

**Observe task firings.**

```text
# dtrace -n 'fbt::taskqueue_run_locked:entry { @[execname] = count(); }'
```

Shows which taskqueue threads are running, which is useful for confirming the private taskqueue is getting work.

**Observe callout firings.**

```text
# dtrace -n 'fbt::callout_reset:entry { @[probefunc] = count(); }'
```

Shows how often callouts are being re-armed, which should match the configured interval rate.

Run each one-liner under your stress workload. The counts should match your expectations. Unexpected imbalance is a debugging starting point.

### WITNESS, INVARIANTS, and the Debug Kernel

The first line of defence remains the debug kernel. If `WITNESS` is unhappy about a lock order, fix the order before shipping. If `INVARIANTS` asserts in `cbuf_*` or `sema_*`, fix the caller before shipping. These checks are cheap; running without them in development is a false economy.

A few `WITNESS` outputs to expect or avoid:

- **"acquiring duplicate lock of same type"**: you acquired a lock you already hold, probably by accident. Review the call path.
- **"lock order reversal"**: two locks were acquired in different orders on different paths. Pick one order, enforce it, update `LOCKING.md`.
- **"blockable sleep from an invalid context"**: you called something that can sleep from a context where sleeping is not allowed. Check whether the context is a callout or interrupt.

Every `WITNESS` warning in `dmesg` from your driver is a bug. Treat them as panic-equivalent; fix each one.

### Regression Discipline

After every Chapter 15 stage, run:

1. The Chapter 11 IO smoke tests (basic read, write, open, close).
2. The Chapter 12 synchronisation tests (bounded reads, sx-protected config).
3. The Chapter 13 timer tests (heartbeat, watchdog, tick source at various rates).
4. The Chapter 14 taskqueue tests (poll waiter, bulk writer flood, delayed reset).
5. The Chapter 15 tests (writer-cap, stats cache, detach under load).

The whole suite should pass on a debug kernel. If any test fails, the regression is recent; roll back to the previous stage, find the delta, and debug.

### Wrapping Up Section 7

Advanced synchronisation needs advanced testing. Stress tests that exercise concurrent writers, readers, and sysctls surface bugs that single-threaded testing misses. DTrace makes the internals of the synchronisation primitives observable. `WITNESS` and `INVARIANTS` catch what remains. Running the whole stack under a debug kernel is the closest thing to "good enough" testing that a driver can get. Section 8 closes Part 3.



## Section 8: Refactoring and Versioning Your Coordinated Driver

Stage 4 is the consolidation stage for Chapter 15. It does not add new functionality; it reorganises and documents the Chapter 15 additions, updates `LOCKING.md`, bumps the version to `0.9-coordination`, and runs the full regression suite across Chapters 11 through 15.

This section walks through the consolidation, extends `LOCKING.md` with the Chapter 15 sections, and closes Part 3.

### File Organisation

The Chapter 15 refactor introduces `myfirst_sync.h`. The file list becomes:

- `myfirst.c`: the main driver source.
- `cbuf.c`, `cbuf.h`: unchanged from Chapter 13.
- `myfirst_sync.h`: new header with synchronisation wrappers.
- `Makefile`: unchanged except for the addition of `myfirst_sync.h` to the headers the source depends on (for `make` dependency tracking if needed).

Inside `myfirst.c`, the Chapter 15 organisation follows the same pattern as Chapter 14 with a few additions:

1. Includes (now includes `myfirst_sync.h`).
2. Softc structure (extended with Chapter 15 fields).
3. File-handle structure (unchanged).
4. cdevsw declaration (unchanged).
5. Buffer helpers (unchanged).
6. Cache helpers (new; `myfirst_stats_cache_refresh`).
7. Condition-variable wait helpers (revised with explicit EINTR/ERESTART handling).
8. Sysctl handlers (extended with writers_limit, stats_cache, recovery).
9. Callout callbacks (revised to use atomic_load_acq_int for is_attached where appropriate).
10. Task callbacks (extended with recovery_task).
11. Cdev handlers (revised to use myfirst_sync_* wrappers).
12. Device methods (attach/detach extended with sema, sx, and atomic-flag discipline).
13. Module glue (version bump).

The key change across every section is the use of `myfirst_sync.h` wrappers where the Chapter 14 code used direct primitive calls. This is visible in attach, detach, and every handler.

### The `LOCKING.md` Update

The Chapter 14 `LOCKING.md` had sections for the mutex, cvs, sx, callouts, and tasks. Chapter 15 adds Semaphores, Coordination, and an updated Lock Order section.

```markdown
## Semaphores

The driver owns one counting semaphore:

- `writers_sema`: caps concurrent writers at `sc->writers_limit`.
  Default limit: 4. Range: 1-64. Configurable via the
  `dev.myfirst.N.writers_limit` sysctl.

Semaphore operations happen outside `sc->mtx`. The internal `sema_mtx`
is not in the documented lock order because it does not conflict with
`sc->mtx`; the driver never holds `sc->mtx` across a `sema_wait` or
`sema_post`.

Lowering `writers_limit` below the current semaphore value is
best-effort: the handler lowers the target and lets new entries
observe the lower cap as the value drains. Raising posts additional
slots immediately.

### Sema Drain Discipline

The driver tracks `writers_inflight` as an atomic int. It is
incremented before any `sema_*` call (specifically at the top of
`myfirst_sync_writer_enter`) and decremented after the matching
`sema_post` (in `myfirst_sync_writer_leave` or on every error
return).

Detach waits for `writers_inflight` to reach zero before calling
`sema_destroy`. This closes the use-after-free race where a woken
waiter is between `cv_wait` return and its final
`mtx_unlock(&sema->sema_mtx)` when `sema_destroy` tears down the
internal mutex.

`sema_destroy` itself is called only after:

1. `is_attached` has been cleared atomically.
2. `writers_limit` wake-up slots have been posted to the sema.
3. `writers_inflight` has been observed to reach zero.
4. Every callout, task, and selinfo has been drained.

## Coordination

The driver uses three cross-component coordination mechanisms:

1. **Atomic is_attached flag.** Read via `atomic_load_acq_int`, written
   via `atomic_store_rel_int` in detach. Allows every context (callout,
   task, user thread) to check shutdown state without acquiring
   `sc->mtx`.
2. **recovery_in_progress state flag.** Protected by `sc->mtx`. Set by
   the watchdog callout, cleared by the recovery task. Ensures at most
   one recovery task is pending or running at a time.
3. **Stats cache sx.** Shared reads, occasional upgrade-promote-
   downgrade for refresh. See the Stats Cache section.

## Stats Cache

The `stats_cache_sx` protects a small cached statistic. The refresh
pattern is:

```c
sx_slock(&sc->stats_cache_sx);
if (stale) {
        if (sx_try_upgrade(&sc->stats_cache_sx)) {
                refresh();
                sx_downgrade(&sc->stats_cache_sx);
        } else {
                sx_sunlock(&sc->stats_cache_sx);
                sx_xlock(&sc->stats_cache_sx);
                if (still_stale)
                        refresh();
                sx_downgrade(&sc->stats_cache_sx);
        }
}
value = sc->stats_cache_bytes_10s;
sx_sunlock(&sc->stats_cache_sx);
```text

## Lock Order

The complete driver lock order is:

```text
sc->mtx  ->  sc->cfg_sx  ->  sc->stats_cache_sx
```text

`WITNESS` enforces this order. The writer-cap semaphore's internal
mutex is not in the graph because the driver never holds `sc->mtx`
(or any other driver lock) across a `sema_wait`/`sema_post` call.

## Detach Ordering (updated)

1. Refuse detach if `sc->active_fhs > 0`.
2. Clear `sc->is_attached` under `sc->mtx` via
   `atomic_store_rel_int`.
3. `cv_broadcast(&sc->data_cv)`; `cv_broadcast(&sc->room_cv)`.
4. Release `sc->mtx`.
5. Post `writers_limit` wake-up slots to `writers_sema`.
6. Wait for `writers_inflight == 0` (sema drain).
7. Drain the three callouts.
8. Drain every task including recovery_task.
9. `seldrain(&sc->rsel)`, `seldrain(&sc->wsel)`.
10. `taskqueue_free(sc->tq)`; `sc->tq = NULL`.
11. `sema_destroy(&sc->writers_sema)` (safe: drain completed).
12. `sx_destroy(&sc->stats_cache_sx)`.
13. Destroy cdev, free sysctl context, destroy cbuf, counters,
    cvs, cfg_sx, mtx.
```

### The Version Bump

The version string advances from `0.8-taskqueues` to `0.9-coordination`:

```c
#define MYFIRST_VERSION "0.9-coordination"
```

And the driver's probe string:

```c
device_set_desc(dev, "My First FreeBSD Driver (Chapter 15 Stage 4)");
```

### The Final Regression Pass

The Chapter 15 regression suite adds its own tests but also re-runs every earlier chapter's tests. A compact ordering:

1. **Build cleanly** on a debug kernel with all the usual options enabled.
2. **Load** the driver.
3. **Chapter 11 tests**: basic read, write, open, close, reset.
4. **Chapter 12 tests**: bounded reads, timed reads, cv broadcasts, sx config.
5. **Chapter 13 tests**: callouts at various rates, watchdog detection, tick source.
6. **Chapter 14 tests**: poll waiter, coalescing flood, delayed reset, detach under load.
7. **Chapter 15 tests**: writer-cap correctness, stats cache concurrency, signal interruption with partial progress, detach under full load.
8. **WITNESS pass**: every test, zero warnings in `dmesg`.
9. **DTrace verification**: wakeup counts and task firings match expectations.
10. **Long-duration stress**: hours of load with periodic detach-reload cycles.

Every test passes. Every `WITNESS` warning is resolved. The driver is at `0.9-coordination` and Part 3 is done.

### Documentation Audit

A final documentation pass.

- `myfirst.c` top-of-file comment is updated with the Chapter 15 vocabulary.
- `myfirst_sync.h` has a top-of-file comment summarising the design.
- `LOCKING.md` has the Semaphores, Coordination, and Stats Cache sections.
- The chapter's `README.md` under `examples/part-03/ch15-more-synchronization/` describes each stage.
- Every sysctl has a description string.

Updating documentation feels like overhead. It is the difference between a driver the next maintainer can change and a driver they have to rewrite.

### A Final Audit Checklist

- [ ] Does every `sema_wait` have a matching `sema_post`?
- [ ] Does every `sx_slock` have a matching `sx_sunlock`?
- [ ] Does every `sx_xlock` have a matching `sx_xunlock`?
- [ ] Does every atomic read use `atomic_load_acq_int` and every atomic write use `atomic_store_rel_int` where ordering matters?
- [ ] Does every `cv_*_sig` call handle EINTR, ERESTART, and EWOULDBLOCK explicitly?
- [ ] Does every blocking-wait caller record and check partial progress?
- [ ] Is every synchronisation primitive wrapped in `myfirst_sync.h`?
- [ ] Does every primitive appear in `LOCKING.md`?
- [ ] Is the detach ordering in `LOCKING.md` accurate?
- [ ] Does the driver pass the full regression suite?

A driver that answers every item cleanly is a driver you can hand to another engineer with confidence.

### Wrapping Up Section 8

Stage 4 consolidates. The header is in place, `LOCKING.md` is current, the version reflects the new capability, the regression suite passes, the audit is clean. The driver is `0.9-coordination` and the synchronisation story is complete.

Part 3 is complete as well. Five chapters, five primitives added one at a time, each composed with what came before. The Wrapping Up section after the labs and challenges frames what Part 3 accomplished and how Part 4 will use it.



## Additional Topics: Atomic Operations, `epoch(9)` Revisited, and Memory Ordering

The main body of Chapter 15 taught the essentials. Three adjacent topics deserve a somewhat deeper mention because they recur in real driver code and because the underlying ideas round out the synchronisation story.

### Atomic Operations in More Depth

The Chapter 15 driver used three atomic primitives: `atomic_load_acq_int`, `atomic_store_rel_int`, and implicitly `atomic_fetchadd_int` via `counter(9)`. The `atomic(9)` family is larger and more structured than these three operations suggest.

**Read-modify-write primitives.**

- `atomic_fetchadd_int(p, v)`: returns the old value of `*p`, sets `*p += v`. Useful for free-running counters.
- `atomic_cmpset_int(p, old, new)`: if `*p == old`, sets `*p = new` and returns 1; otherwise returns 0 and does not modify. The classic compare-and-swap. Useful for implementing lock-free state machines.
- `atomic_cmpset_acq_int`, `atomic_cmpset_rel_int`: variants with acquire or release semantics.
- `atomic_readandclear_int(p)`: returns the old value and sets `*p = 0`. Useful for "take the current value, reset".
- `atomic_set_int(p, v)`: sets bits `*p |= v`. Useful for flag-set coordination.
- `atomic_clear_int(p)`: clears bits `*p &= ~v`. Useful for flag-clear coordination.
- `atomic_swap_int(p, v)`: returns old `*p`, sets `*p = v`. Useful for taking ownership of a pointer.

**Width variants.** `atomic_load_int`, `atomic_load_long`, `atomic_load_32`, `atomic_load_64`, `atomic_load_ptr`. The integer size in the name matches the C type. Use the one that matches your variable's type.

**Barrier variants.** `atomic_thread_fence_acq`, `atomic_thread_fence_rel`, `atomic_thread_fence_acq_rel`, `atomic_thread_fence_seq_cst`. Pure barriers that order preceding and following memory accesses without atomically modifying a specific location. Occasionally useful.

Picking the right variant is a small discipline. For a flag that readers poll, use `atomic_load_acq`. For a writer that commits a flag after preceding setup, use `atomic_store_rel`. For a counter that is free-running and readers never synchronise on exactly, use `atomic_fetchadd` (no barriers). For a CAS-based state machine, use `atomic_cmpset`.

### `epoch(9)` In One Page

Chapter 14 introduced `epoch(9)` briefly; here is a slightly deeper sketch, within the boundary of what a driver writer usefully knows.

An epoch is a short-lived synchronisation barrier that protects read-mostly data structures without locks. Code that reads shared data enters the epoch with `epoch_enter(epoch)` and leaves with `epoch_exit(epoch)`. The epoch guarantees that any object freed via `epoch_call(epoch, cb, ctx)` is not actually reclaimed until every reader that was inside the epoch when the free was requested has exited.

This is similar in spirit to Linux's RCU (read-copy-update) but with different ergonomics. The FreeBSD epoch is a coarser tool; it protects large, rarely-changing structures like the ifnet list.

A driver that wants to use epoch typically does not create its own. It uses one of the kernel-provided epochs, most commonly `net_epoch_preempt` for network state. Driver writers outside of network code rarely reach for epoch directly.

What a driver writer should know is how to recognise the pattern in other code, and when a taskqueue's `NET_TASK_INIT` is creating a task that runs inside an epoch. Section 14 covered this.

### Memory Ordering, A Little Deeper

The atomic API hides the architecture-specific details of memory ordering. When you use `atomic_store_rel_int` and `atomic_load_acq_int` in matching pairs, the right barrier is inserted on each architecture. You do not need to know the details.

But a one-page intuition helps.

On x86, every load is an acquire-load and every store is a release-store, at the hardware level. The CPU's memory model is "total store order". So `atomic_load_acq_int` on x86 is just a plain `MOV` with no extra instructions. `atomic_store_rel_int` is a plain `MOV` too.

On arm64, loads and stores have weaker default ordering. The compiler inserts `LDAR` (load-acquire) for `atomic_load_acq_int` and `STLR` (store-release) for `atomic_store_rel_int`. These are cheap (a few cycles) but not free.

The implication: correctness-wise, you write the same code on both architectures. Performance-wise, x86 "pays" nothing for the barriers while arm64 pays a small cost. For a rare operation like a shutdown flag, the cost is negligible on both.

The further implication: testing on x86 alone is insufficient for validating memory ordering. Code that works on x86 with plain loads might deadlock or misbehave on arm64 if the atomic barriers were omitted. FreeBSD 14.3 supports arm64 well; drivers shipped to users who run on arm64 hardware need to be correct on the weaker memory model. Using the `atomic(9)` API consistently is how you guarantee this without thinking about architecture per-call.

### When to Reach for Each Tool

A small decision tree to close the section.

- **Protect a small invariant read by many, written rarely?** `atomic_load_acq_int` / `atomic_store_rel_int`.
- **Protect a small invariant with an intricate relation to other state?** Mutex.
- **Wait for a predicate?** Mutex plus condition variable.
- **Wait for a predicate with signal handling?** Mutex plus `cv_wait_sig` or `cv_timedwait_sig`.
- **Admit at most N participants?** `sema(9)`.
- **Many readers or one writer with occasional promotion?** `sx(9)` with `sx_try_upgrade`.
- **Run code later in thread context?** `taskqueue(9)`.
- **Run code at a deadline?** `callout(9)`.
- **Coordinate shutdown across contexts?** Atomic flag + cv broadcast (for blocked waiters).
- **Protect a read-mostly structure in network code?** `epoch(9)`.

That decision tree is the mental map Part 3 has been building. Chapter 15 added the last few branches. Chapter 16 will apply the map to hardware.

### Wrapping Up Additional Topics

Atomic operations, `epoch(9)`, and memory ordering round out the synchronisation toolkit. For most drivers the common case is a mutex plus a cv plus occasional atomics; the other primitives are there for specific shapes. Knowing the whole set lets you pick the right tool without the guesswork.



## Hands-On Labs

Four labs apply the Chapter 15 material to concrete tasks. Allocate one session per lab. Labs 1 and 2 are the most important; Labs 3 and 4 stretch the reader.

### Lab 1: Observe Writer-Cap Enforcement

**Objective.** Confirm that the writer-cap semaphore limits concurrent writers and that the limit is configurable at runtime.

**Setup.** Build and load the Stage 4 driver. Compile the `writer_cap_test` helper from the companion source.

**Steps.**

1. Verify the default limit: `sysctl dev.myfirst.0.writers_limit`. Should be 4.
2. Check the semaphore value: `sysctl dev.myfirst.0.stats.writers_sema_value`. Should also be 4 (no writers active).
3. Start sixteen blocking writers in the background:
   ```text
   # for i in $(jot 16 1); do
       (cat /dev/urandom | head -c 10000 > /dev/myfirst) &
   done
   ```
4. Observe the semaphore value while they run. Should be near zero most of the time (all slots in use).
5. Lower the limit to 2:
   ```text
   # sysctl dev.myfirst.0.writers_limit=2
   ```
6. Observe that the semaphore value eventually drops to 0 and stays there (writers drain faster than they re-enter).
7. Raise the limit to 8:
   ```text
   # sysctl dev.myfirst.0.writers_limit=8
   ```
8. The driver posts four additional slots immediately; writers that were blocked in `sema_wait` wake up and enter.
9. Wait for all writers to finish; verify the final semaphore value equals the current limit.

**Expected outcome.** The semaphore acts as the admission controller; reconfiguring it at runtime reconfigures the limit. Under heavy load the sema drains to zero; when load eases, it refills to the configured limit.

**Variation.** Try with non-blocking writers using a helper that opens with `O_NONBLOCK`. Watch `sysctl dev.myfirst.0.stats.writers_trywait_failures` grow when the sema is exhausted.

### Lab 2: Stats Cache Contention

**Objective.** Observe that the stats cache serves many reads with few refreshes under a read-mostly workload.

**Setup.** Stage 4 driver loaded. DTrace available.

**Steps.**

1. Start 32 concurrent reader processes, each reading the cached stat in a tight loop:
   ```text
   # for i in $(jot 32 1); do
       (while :; do
           sysctl -n dev.myfirst.0.stats.bytes_written_10s >/dev/null
       done) &
   done
   ```
2. In a separate terminal, observe the cache refresh rate via DTrace:
   ```text
   # dtrace -n 'fbt::myfirst_stats_cache_refresh:entry { @ = count(); }'
   ```
3. Let the workload run for 30 seconds, then Ctrl-C DTrace. Record the count.

**Expected outcome.** Approximately 30 refreshes over 30 seconds: one per second, regardless of how many readers are reading. If you see substantially more refreshes, the cache is being invalidated too aggressively; if substantially fewer, the readers are not actually triggering the stale path.

**Variation.** Run the test with a write workload in parallel (also using `/dev/myfirst`). The refresh rate should not change: the refresh is triggered by cache staleness, not by writes.

### Lab 3: Signal Handling and Partial Progress

**Objective.** Confirm that a `read(2)` interrupted by a signal returns the bytes copied so far, not EINTR.

**Setup.** Stage 4 driver loaded. Stopped tick source, empty buffer.

**Steps.**

1. Start a reader that asks for 4096 bytes with no timeout:
   ```text
   # dd if=/dev/myfirst bs=4096 count=1 > /tmp/out 2>&1 &
   # READER=$!
   ```
2. Enable the tick source at a slow rate:
   ```text
   # sysctl dev.myfirst.0.tick_source_interval_ms=500
   ```
   The reader slowly accumulates bytes, one every 500 ms.
3. After about 2 seconds, send SIGINT to the reader:
   ```text
   # kill -INT $READER
   ```
4. `dd` reports the number of bytes copied before the signal.

**Expected outcome.** `dd` reports a partial result (say, 4 bytes copied out of 4096 requested) and exits 0 (partial success), not an error. The driver returned the partial byte count to the syscall layer; the syscall layer surfaced it as a normal short read.

**Variation.** Set `read_timeout_ms` to 1000 and repeat. The driver should return either a partial result (if any bytes arrived) or EAGAIN (if the timeout fired first with zero bytes). Signal handling should still preserve partial bytes.

### Lab 4: Detach Under Maximum Load

**Objective.** Confirm that the detach ordering is correct under full concurrent load.

**Setup.** Stage 4 driver loaded. All the Chapter 14 and 15 stress tools compiled.

**Steps.**

1. Start the full stress kit:
   - 8 concurrent writers.
   - 4 concurrent readers.
   - Tick source at 1 ms.
   - Heartbeat at 100 ms.
   - Watchdog at 1 s.
   - Sysctl flood every 100 ms: `bulk_writer_flood=1000`.
   - Sysctl flood every 500 ms: adjust `writers_limit` between 1 and 8.
   - Concurrent sysctl reads of `stats.bytes_written_10s`.
2. Let the stress run for 30 seconds to ensure maximum load.
3. Unload the driver:
   ```text
   # kldunload myfirst
   ```
4. Kill the stress processes. Observe that the unload completed cleanly.

**Expected outcome.** The unload succeeds (not EBUSY, not a panic, no hang). All stress processes fail gracefully (open returning ENXIO, outstanding reads/writes returning ENXIO or short results). `dmesg` has no `WITNESS` warnings.

**Variation.** Repeat the cycle 20 times (load, stress, unload). Every cycle should behave identically. If one cycle panics or hangs, there is a race; investigate.



## Challenge Exercises

The challenges go beyond the chapter's body. They are optional; each consolidates a specific Chapter 15 idea.

### Challenge 1: Replace sema with Interruptible Wait

The writer-cap uses `sema_wait`, which is not signal-interruptible. Rewrite the write path's admission control to use `sema_trywait` plus an interruptible `cv_wait_sig` loop. Preserve the partial-progress convention.

Expected outcome: a writer blocked waiting for a slot can be SIGINT'd cleanly. Bonus: use the encapsulation in `myfirst_sync.h` so `myfirst_sync_writer_enter` has the same signature but different internals.

### Challenge 2: Read-Mostly with a Background Refresher

The stats cache refreshes on-demand when a reader notices staleness. Change the design so the cache is refreshed by a periodic callout instead, and readers never trigger a refresh. Compare the resulting code to the upgrade-promote-downgrade pattern.

Expected outcome: understand when on-demand caching is better than background caching. Answer: on-demand is simpler (no callout lifetime to manage) but wastes a refresh on a cache that nobody reads. Background is more predictable but requires the extra primitive. Most drivers pick on-demand for small caches and background for large ones.

### Challenge 3: Multiple Writer-Cap Semaphores

Imagine the driver is layered over a storage backend that has separate pools for different classes of IO: "small writes" and "large writes". Add a second semaphore that caps large writes separately, with its own limit. Writers entering the write path choose which semaphore to acquire based on their `uio_resid`.

Expected outcome: practical experience with multiple semaphores. Think through: if a writer acquires the "large" sema, then the `uio` turns out to be small, does the write path want to release and re-acquire? Or keep the acquired slot? Document your choice.

### Challenge 4: Atomic-Based Recovery Flag Elimination

Replace the `recovery_in_progress` state flag with an atomic compare-and-swap. The watchdog does `atomic_cmpset_int(&sc->recovery_in_progress, 0, 1)`; on success it enqueues the task. The task clears the flag with `atomic_store_rel_int`.

Expected outcome: the recovery mechanism no longer requires the mutex. Compare the two implementations in terms of correctness, complexity, and observability.

### Challenge 5: Epoch for a Hypothetical Read Path

Study `/usr/src/sys/sys/epoch.h` and `/usr/src/sys/kern/subr_epoch.c`. Sketch (in comments, not in code) how you would convert the read path to use a private epoch that protects a "current configuration" pointer that writers occasionally update.

Expected outcome: a written proposal with the trade-offs. Since the driver's current config is small and sx already handles it well, this is a thought exercise rather than a practical refactor. The point is to understand when epoch would be a win.

### Challenge 6: Stress Test a Specific Race

Pick one race condition from Section 7. Write a script that triggers it reliably. Verify that the Stage 4 driver does not exhibit the bug. Verify that a deliberately broken version (revert the fix) does exhibit the bug.

Expected outcome: the satisfaction of seeing a race condition produced on demand, and the reassurance that the production code handles it.

### Challenge 7: Read a Real Driver's Synchronization Vocabulary

Open `/usr/src/sys/dev/bge/if_bge.c` (or a similar mid-size driver with many primitives). Walk through its attach and detach paths. Count:

- Mutexes.
- Condition variables.
- Sx locks.
- Semaphores (if any; many drivers have none).
- Callouts.
- Tasks.
- Atomic operations.

Write a one-page summary of the driver's synchronisation strategy. Compare to `myfirst`. What does the real driver do differently, and why?

Expected outcome: reading a real driver's synchronisation strategy is the fastest way to make your own feel familiar. After one such read, opening any other driver in `/usr/src/sys/dev/` becomes easier.



## Troubleshooting Reference

A flat reference list for common Chapter 15 problems.

### Semaphore Deadlock or Leak

- **Writers pile up and never proceed.** The semaphore's counter is zero and no one posts. Check: is every `sema_wait` matched by a `sema_post`? Is there an early-return path that forgets the post?
- **`sema_destroy` panics with "waiters" assertion.** The destroy happened while a thread was still inside `sema_wait`. Fix: ensure the detach path quiesces all potential waiters before destroying. Usually this means clearing `is_attached` and doing a cv broadcast first.
- **`sema_trywait` returns unexpected values.** Remember: 1 on success, 0 on failure. Inverse of most FreeBSD APIs. Recheck the call-site logic.

### Sx Lock Issues

- **`sx_try_upgrade` fails always.** The calling thread probably shares the sx with another reader. Check: is there a path that holds `sx_slock` persistently in a different thread?
- **Deadlock between sx and another lock.** Lock order violation. Run under `WITNESS`; the kernel will name the violation.
- **`sx_downgrade` with no `sx_try_upgrade` pair.** Make sure the sx is actually held exclusively before downgrading. `sx_xlocked(&sx)` asserts this.

### Signal-Handling Issues

- **`read(2)` does not respond to SIGINT.** The blocking wait is `cv_wait` (not `cv_wait_sig`), or `sema_wait`. Convert to the signal-interruptible variant.
- **`read(2)` returns EINTR with partial bytes lost.** The partial-progress check is missing. Add the `uio_resid != nbefore` check on the signal-error path.
- **`read(2)` loops after EINTR.** The loop continues on signal-error instead of returning. Add the EINTR/ERESTART handling.

### Atomic and Memory Ordering

- **Shutdown flag check misses.** A context reads `sc->is_attached` with a plain load and sees a stale value. Convert to `atomic_load_acq_int`.
- **Write order not observed on arm64.** Writes were reordered across architectures. Use `atomic_store_rel_int` for the last write in the pre-detach sequence.

### Coordination Bugs

- **Recovery task runs multiple times.** The state flag is not protected or the atomic CAS is misused. Either use the mutex-protected flag pattern or audit the CAS logic.
- **Recovery task never runs.** The flag is never cleared, or the watchdog is not enqueuing. Check which side has the bug with a `device_printf` on each path.

### Testing Issues

- **Stress test passes sometimes and fails others.** A race with low probability. Run many more iterations, increase concurrency, or add timing noise (`usleep` at random points) to expose it.
- **DTrace probes do not fire.** The kernel was built without FBT probes, or the function was inlined away. Check `dtrace -l | grep myfirst`.
- **WITNESS warnings flood the logs.** Do not ignore them. Every warning is a real bug. Fix one at a time and iterate.

### Detach Issues

- **`kldunload` returns EBUSY.** Open file descriptors still exist. Close them and retry.
- **`kldunload` hangs.** A drain is waiting for a primitive that cannot complete. Usually a task or callout. Use `procstat -kk` on the kldunload thread to find where it is stuck.
- **Kernel panic during unload.** Use-after-free in a task or callout callback; the ordering is wrong. Review the detach sequence in `LOCKING.md` against the actual code.



## Wrapping Up Part 3

Chapter 15 is the last chapter of Part 3. Part 3 had a specific mission: give the `myfirst` driver a full synchronisation story, from the first mutex in Chapter 11 to the last atomic flag in Chapter 15. The mission is complete.

A short inventory of what Part 3 delivered.

### The Mutex (Chapter 11)

The first primitive. A sleep mutex protects the driver's shared data from concurrent access. Every path that touches the cbuf, the open count, the active-fh count, or the other mutex-protected fields acquires `sc->mtx` first. `WITNESS` enforces the rule.

### The Condition Variable (Chapter 12)

The waiting primitive. Two cvs (`data_cv`, `room_cv`) let readers and writers sleep until the buffer state is acceptable. `cv_wait_sig` and `cv_timedwait_sig` make the waits signal-interruptible and time-bounded.

### The Shared/Exclusive Lock (Chapter 12)

The read-mostly primitive. `sc->cfg_sx` protects the configuration structure. Shared acquisition for reads, exclusive for writes. Chapter 15 added `sc->stats_cache_sx` with the upgrade-promote-downgrade pattern.

### The Callout (Chapter 13)

The time primitive. Three callouts (heartbeat, watchdog, tick source) give the driver internal time without requiring a dedicated thread. `callout_init_mtx` makes them lock-aware; `callout_drain` makes them tear down safely.

### The Taskqueue (Chapter 14)

The deferred-work primitive. A private taskqueue with three tasks (selwake, bulk writer, recovery) moves work from constrained contexts to thread context. The detach sequence drains every task before freeing the queue.

### The Semaphore (Chapter 15)

The bounded-admission primitive. `writers_sema` caps concurrent writers. The API is small and ownership-free; the driver uses `sema_trywait` for non-blocking entries and `sema_wait` for blocking.

### Atomic Operations (Chapter 15)

The cross-context flag. `atomic_load_acq_int` and `atomic_store_rel_int` on `is_attached` make the shutdown flag visible to every context with correct memory ordering.

### Encapsulation (Chapter 15)

The maintenance primitive. `myfirst_sync.h` wraps every synchronisation operation in a named function. A future reader understands the driver's synchronisation strategy by reading one header.

### What the Driver Can Now Do

A short inventory of the driver's capabilities at the end of Part 3:

- Serve concurrent readers and writers on a bounded ring buffer.
- Block readers until data arrives, with optional timeout.
- Block writers until room is available, with optional timeout.
- Cap concurrent writers via a configurable semaphore.
- Expose configuration (debug level, nickname, soft byte limit) protected by an sx lock.
- Emit a periodic heartbeat log line.
- Detect stalled buffer drainage via a watchdog.
- Inject synthetic data via a tick source callout.
- Defer `selwakeup` out of the callout callback via a task.
- Demonstrate task coalescing via a configurable bulk writer.
- Schedule a delayed reset via a timeout task.
- Expose a cached statistic via an upgrade-aware sx pattern.
- Coordinate detach across every primitive without races.
- Answer signals correctly during blocking operations.
- Respect partial-progress semantics on read and write.

That is a substantial driver. The `myfirst` module at `0.9-coordination` is a compact but complete example of the synchronisation patterns that every real FreeBSD driver uses. The patterns transfer.

### What Part 3 Did Not Cover

A short list of topics Part 3 deliberately left for later parts:

- Hardware interrupts (Part 4).
- Memory-mapped register access (Part 4).
- DMA and bus space operations (Part 4).
- PCI device matching (Part 4).
- USB and network-specific subsystems (Part 6).
- Advanced performance tuning (specialised chapters later).

Part 3 focused on the internal synchronisation story. Part 4 will add the hardware-facing story. The synchronisation story does not go away; it becomes the foundation on which the hardware story rests.

### A Reflection

You started Chapter 11 with a driver that supported one user at a time. You end Chapter 15 with a driver that supports many users, coordinates several kinds of work, and tears down cleanly under load. Along the way you learned the kernel's main synchronisation primitives, each introduced with a specific invariant in mind, each composed with what came before.

The learning pattern was deliberate. Each chapter introduced one new concept, applied it to the driver in a small refactor, documented it in `LOCKING.md`, and added regression tests. The result is a driver whose synchronisation is not an accident. Every primitive is there for a reason; every primitive is documented; every primitive is tested.

That discipline is the most durable thing Part 3 teaches. The specific primitives (mutex, cv, sx, callout, taskqueue, sema, atomic) are the currency, but the discipline of "pick the right primitive, document it, test it" is the investment. Drivers built with that discipline survive growth, maintenance handoffs, and surprising load patterns. Drivers built without it accumulate subtle bugs and hard-to-explain crashes.

Part 4 opens the door to hardware. The primitives you now know stay with you. The discipline you have practised is what will let you add the hardware-facing story without getting lost.

Take a moment. This is a real accomplishment. Then move to Chapter 16.

## Part 3 Checkpoint

Five chapters of synchronization is a lot of material. Before Part 4 opens the door to hardware, it is worth confirming that the primitives and the discipline have settled.

By the end of Part 3 you should be able to do each of the following confidently:

- Choose among `mutex(9)`, `sx(9)`, `rw(9)`, `cv(9)`, `callout(9)`, `taskqueue(9)`, `sema(9)`, and the `atomic(9)` family with a clear sense of which invariant each one is appropriate for, rather than by habit or guesswork.
- Document a driver's locking in a `LOCKING.md` that names every primitive, the invariant it enforces, the data it protects, and the rules callers must follow.
- Implement a sleep-and-wake handshake using either `mtx_sleep`/`wakeup` or `cv_wait`/`cv_signal`, and explain why you chose one over the other.
- Schedule timed work with `callout(9)`, including cancellation under detach, without leaving dangling timers behind.
- Defer heavy or ordered work through `taskqueue(9)`, including the detach-time drain that prevents tasks from running against freed state.
- Keep a `myfirst` running cleanly under `INVARIANTS` and `WITNESS` kernels while multi-threaded stress tests hammer every entry point.

If any of those are still vague, revisit the labs that introduced them:

- Locking discipline and regression: Lab 11.2 (Verify the Locking Discipline with INVARIANTS), Lab 11.4 (Build a Multi-threaded Tester), and Lab 11.7 (Long-Running Stress).
- Condition variables and sx: Lab 12.2 (Add Bounded Reads), Lab 12.5 (Detect a Deliberate Lock Order Reversal), and Lab 12.7 (Verify the Snapshot-and-Apply Pattern Holds Under Contention).
- Callouts and timed work: Lab 13.1 (Add a Heartbeat Callout) and Lab 13.4 (Verify Detach With Active Callouts).
- Taskqueues and deferred work: Lab 2 (Measuring Coalescing Under Load) and Lab 3 (Verify the Detach Ordering) in Chapter 14.
- Semaphores: Lab 1 (Observe Writer-Cap Enforcement) and Lab 4 (Detach Under Maximum Load) in Chapter 15.

Part 4 will layer hardware onto everything Part 3 just built. Specifically, the chapters that follow will expect:

- The synchronization model internalized rather than memorized, so that an interrupt context can be added as one more kind of caller rather than as a new universe of rules.
- Detach ordering treated as a single, shared discipline across primitives, since Part 4 will add interrupt teardown and bus-resource release into the same chain.
- Continued comfort with `INVARIANTS` and `WITNESS` as the default development kernel, since Part 4's harder bugs will usually trip one of the two long before they surface as a visible panic.

If those hold, Part 4 is within reach. If one still feels shaky, the fix is a lap through the relevant lab rather than a push forward.

## Bridge to Chapter 16

Chapter 16 opens Part 4 of the book. Part 4 is titled *Hardware and Platform-Level Integration*, and Chapter 16 is *Hardware Basics and Newbus*. Part 4's mission is to give the driver a hardware story: how a driver announces itself to the kernel's bus layer, how it is matched against hardware the kernel has discovered, how it receives interrupts, how it accesses memory-mapped registers, and how it manages DMA.

The synchronisation story from Part 3 does not go away. It becomes the foundation on which Part 4 builds. A hardware interrupt handler runs in a context you now know how to reason about (no sleeping, no sleepable locks, no uiomove). It communicates with the rest of the driver through primitives you now know how to use (taskqueues for deferred work, mutexes for serialisation, atomic flags for shutdown). The difference is that the driver now also has to talk to the hardware directly, and the hardware has its own rules.

Chapter 16 prepares the hardware ground in three specific ways.

First, **you already know about context boundaries**. Part 3 taught you that callouts, tasks, and user threads each have their own rules. Interrupts add one more context with stricter rules. The mental model ("which context am I in; what can I safely do here") transfers directly.

Second, **you already know about detach ordering**. Part 3 built up a detach discipline across five primitives. Part 4 adds two more (interrupt teardown, resource release) that slot into the same discipline. The ordering rules grow; the shape does not.

Third, **you already know about `LOCKING.md` as a living document**. Chapter 16 adds a Hardware Resources section. The discipline is the same; the vocabulary extends.

Specific topics Chapter 16 will cover:

- The `newbus(9)` framework: how drivers are identified, probed, and attached.
- `device_t`, `devclass`, `driver_t`, and `device_method_t`.
- `bus_alloc_resource` and `bus_release_resource` for memory-mapped regions, IRQ lines, and other resources.
- `bus_setup_intr` and `bus_teardown_intr` for interrupt registration.
- Filter handlers versus interrupt threads.
- The relationship between newbus and the PCI subsystem (preparing for Chapter 17).

You do not need to read ahead. Chapter 15 is sufficient preparation. Bring your `myfirst` driver at `0.9-coordination`, your `LOCKING.md`, your `WITNESS`-enabled kernel, and your test kit. Chapter 16 starts where Chapter 15 ended.

A small closing reflection. The driver you started Part 3 with knew how to serve one syscall. The driver you have now has a full internal synchronisation story, with six kinds of primitives, each selected for a specific shape of invariant, each encapsulated in a readable wrapper layer, each documented, each tested. It is ready to face the hardware.

The hardware is the next chapter. Then the one after that. Then every chapter of Part 4. The foundation is built. The tools are on the bench. The blueprint is ready.

Turn the page.



## Reference: Pre-Production Synchronisation Audit

Before shipping a synchronisation-heavy driver, run through this audit. Each item is a question; each should be answerable with confidence.

### Mutex Audit

- [ ] Does every `sc->mtx` hold region end in an `mtx_unlock` on every path?
- [ ] Is the mutex always released before calling `uiomove`, `copyin`, `copyout`, `selwakeup`, or any other sleepable operation?
- [ ] Are there any places the mutex is held across a cv wait? If so, is the wait the intended primitive?
- [ ] Is the lock order `sc->mtx -> sc->cfg_sx -> sc->stats_cache_sx` respected everywhere?

### Condition Variable Audit

- [ ] Does every cv wait call `cv_wait_sig` or `cv_timedwait_sig` in a syscall context?
- [ ] Is EINTR handled with partial-progress preservation where appropriate?
- [ ] Is ERESTART propagated correctly?
- [ ] Does every cv have a matching broadcast on detach?
- [ ] Are wakeups done with the mutex held for correctness (or the mutex released for throughput, with the trade-off documented)?

### Sx Audit

- [ ] Does every `sx_slock` have a matching `sx_sunlock`?
- [ ] Does every `sx_xlock` have a matching `sx_xunlock`?
- [ ] Does every `sx_try_upgrade` failure path correctly handle the re-check after dropping and re-acquiring?
- [ ] Does every `sx_downgrade` happen on a lock that is actually held exclusively?

### Semaphore Audit

- [ ] Does every `sema_wait` have a matching `sema_post` on every path?
- [ ] Is the semaphore destroyed only after all waiters have been quiesced?
- [ ] Is `sema_trywait`'s return value (1 on success, 0 on failure) read correctly?
- [ ] If the semaphore is used with interruptible syscalls, is the non-interruptibility of `sema_wait` documented?
- [ ] Is there an in-flight counter (e.g., `writers_inflight`) that detach drains before calling `sema_destroy`?
- [ ] Does every path between counter increment and counter decrement actually use the sema (no early returns that bypass the increment)?

### Callout Audit

- [ ] Does every callout use `callout_init_mtx` with the appropriate lock?
- [ ] Does every callout callback check `is_attached` and exit early if false?
- [ ] Is every callout drained at detach before the state it touches is freed?

### Task Audit

- [ ] Does every task callback hold the appropriate locks when touching shared state?
- [ ] Does every task callback call `selwakeup` only with no driver lock held?
- [ ] Is every task drained at detach after the callouts that enqueue it have been drained?
- [ ] Is the private taskqueue freed after every task is drained?

### Atomic Audit

- [ ] Does every shutdown flag read use `atomic_load_acq_int`?
- [ ] Does every shutdown flag write use `atomic_store_rel_int`?
- [ ] Are any other atomic operations justified by a specific memory-ordering requirement?

### Cross-Component Audit

- [ ] Does every cross-component state flag have a clear ownership (which path sets, which path clears)?
- [ ] Are the flags protected by the appropriate lock or atomic discipline?
- [ ] Is the handshake documented in `LOCKING.md`?

### Documentation Audit

- [ ] Does `LOCKING.md` list every primitive?
- [ ] Does `LOCKING.md` document the detach ordering?
- [ ] Does `LOCKING.md` document the lock order?
- [ ] Are any subtle cross-component handshakes explained?

### Testing Audit

- [ ] Has the driver been run under `WITNESS` for an extended stress test with no warnings?
- [ ] Have detach cycles been tested under full load?
- [ ] Have signal-interrupt tests confirmed partial-progress preservation?
- [ ] Have configuration changes at runtime (sysctl tuning) been tested?

A driver that passes this audit is a driver you can ship.



## Reference: Synchronisation Primitive Cheat Sheet

### When to Use Which

| Primitive | Best for | Not good for |
|---|---|---|
| `struct mtx` (MTX_DEF) | Short critical sections; mutual exclusion. | Waiting for a condition. |
| `struct cv` + mtx | Waiting for a predicate; signalled wakeups. | Bounded admission. |
| `struct sx` | Read-mostly state; shared reads with occasional writes. | Hot contention. |
| `struct sema` | Bounded admission; producer-consumer completion. | Interruptible waits. |
| `callout` | Time-based work. | Work that must sleep. |
| `taskqueue` | Deferred thread-context work. | Sub-microsecond latency. |
| `atomic_*` | Small cross-context flags; lock-free coordination. | Complex invariants. |
| `epoch` | Read-mostly shared structures in network code. | Drivers without shared structures. |

### API Quick Reference

**Mutex.**
- `mtx_init(&mtx, name, type, MTX_DEF)`
- `mtx_lock(&mtx)`, `mtx_unlock(&mtx)`
- `mtx_assert(&mtx, MA_OWNED)`
- `mtx_destroy(&mtx)`

**Condition variable.**
- `cv_init(&cv, name)`
- `cv_wait(&cv, &mtx)`, `cv_wait_sig`
- `cv_timedwait(&cv, &mtx, timo)`, `cv_timedwait_sig`
- `cv_signal(&cv)`, `cv_broadcast(&cv)`
- `cv_destroy(&cv)`

**Sx.**
- `sx_init(&sx, name)`
- `sx_slock(&sx)`, `sx_sunlock(&sx)`
- `sx_xlock(&sx)`, `sx_xunlock(&sx)`
- `sx_try_upgrade(&sx)`, `sx_downgrade(&sx)`
- `sx_xlocked(&sx)`, `sx_xholder(&sx)`
- `sx_destroy(&sx)`

**Semaphore.**
- `sema_init(&s, value, name)`
- `sema_wait(&s)`, `sema_timedwait(&s, timo)`, `sema_trywait(&s)`
- `sema_post(&s)`
- `sema_value(&s)`
- `sema_destroy(&s)`

**Callout.**
- `callout_init_mtx(&co, &mtx, 0)`
- `callout_reset(&co, ticks, fn, arg)`
- `callout_stop(&co)`, `callout_drain(&co)`

**Taskqueue.**
- `TASK_INIT(&t, 0, fn, ctx)`, `TIMEOUT_TASK_INIT(...)`
- `taskqueue_create(name, flags, enqueue, ctx)`
- `taskqueue_start_threads(&tq, count, pri, name, ...)`
- `taskqueue_enqueue(tq, &t)`, `taskqueue_enqueue_timeout(...)`
- `taskqueue_cancel(tq, &t, &pend)`, `taskqueue_drain(tq, &t)`
- `taskqueue_free(tq)`

**Atomic.**
- `atomic_load_acq_int(p)`, `atomic_store_rel_int(p, v)`
- `atomic_fetchadd_int(p, v)`, `atomic_cmpset_int(p, old, new)`
- `atomic_set_int(p, v)`, `atomic_clear_int(p, v)`
- `atomic_thread_fence_seq_cst()`

### Context Rules

| Context | Can sleep? | Sleepable locks? | Notes |
|---|---|---|---|
| Syscall | Yes | Yes | Full thread context. |
| Callout callback (lock-aware) | No | No | Registered mutex held. |
| Task callback (thread-backed) | Yes | Yes | No driver lock held. |
| Task callback (fast/swi) | No | No | SWI context. |
| Interrupt filter | No | No | Very limited. |
| Interrupt thread | No | No | Slightly more than filter. |
| Epoch section | No | No | Very limited. |

### Partial Progress Convention

A `read(2)` or `write(2)` that copies N bytes before being interrupted should return N (as a successful partial short read/write), not EINTR. The driver's wait helper returns a sentinel (`MYFIRST_WAIT_PARTIAL`) on the partial path; the caller converts it to 0 so the syscall layer returns the byte count.

### Detach Ordering

The canonical order for Chapter 15 detach:

1. Refuse if `active_fhs > 0`.
2. Clear `is_attached` atomically.
3. Broadcast all cvs; release mutex.
4. Drain all callouts.
5. Drain all tasks (including timeout tasks and recovery).
6. `seldrain` for rsel, wsel.
7. Free taskqueue.
8. Destroy semaphore.
9. Destroy stats-cache sx.
10. Destroy cdev, sysctls, cbuf, counters.
11. Destroy cvs, cfg sx, mutex.

Memorise the shape. Adapt the order when you add new primitives.



## Reference: Further Reading

### Manual Pages

- `sema(9)`: kernel semaphores.
- `sx(9)`: shared/exclusive locks.
- `mutex(9)`: mutex primitives.
- `condvar(9)`: condition variables.
- `atomic(9)`: atomic operations.
- `epoch(9)`: epoch-based synchronisation.
- `locking(9)`: overview of kernel locking primitives.

### Source Files

- `/usr/src/sys/kern/kern_sema.c`: semaphore implementation.
- `/usr/src/sys/sys/sema.h`: semaphore API.
- `/usr/src/sys/kern/kern_sx.c`: sx implementation.
- `/usr/src/sys/sys/sx.h`: sx API.
- `/usr/src/sys/kern/kern_mutex.c`: mutex implementation.
- `/usr/src/sys/kern/kern_condvar.c`: cv implementation.
- `/usr/src/sys/kern/subr_epoch.c`: epoch implementation.
- `/usr/src/sys/sys/epoch.h`: epoch API.
- `/usr/src/sys/dev/hyperv/storvsc/hv_storvsc_drv_freebsd.c`: real-world `sema` usage.
- `/usr/src/sys/dev/bge/if_bge.c`: rich synchronisation example.

### Books and External Material

- *The Design and Implementation of the FreeBSD Operating System* (McKusick et al.): has detailed chapters on the kernel synchronisation subsystems.
- *FreeBSD Handbook*, developer section: sections on kernel locking.
- FreeBSD mailing list archives: searches for primitive names (`taskqueue`, `sema`, `sx`) reveal historical design discussions.

### Suggested Reading Order

For a reader new to advanced synchronisation:

1. `mutex(9)`, `condvar(9)`: the basic primitives.
2. `sx(9)`: the read-mostly primitive.
3. `sema(9)`: the bounded-admission primitive.
4. `atomic(9)`: the cross-context tool.
5. `epoch(9)`: the network-drivers' read-lock-free tool.
6. A real driver source: `/usr/src/sys/dev/bge/if_bge.c` or similar.

Reading in order takes a full afternoon and gives you a solid mental map.



## Reference: A Glossary of Chapter 15 Terms

**Counting semaphore.** A primitive that holds a non-negative integer and supports wait (decrement, blocking if zero) and post (increment, waking one waiter).

**Binary semaphore.** A counting semaphore that only holds 0 or 1. Behaviourally similar to a mutex but without ownership.

**Priority inheritance.** A scheduler technique where a high-priority thread waiting on a lock temporarily raises the priority of the current holder. FreeBSD mutexes support it; semaphores do not.

**Signal-interruptible wait.** A blocking primitive (e.g. `cv_wait_sig`) that returns with EINTR or ERESTART when a signal arrives. The caller can then abandon the wait and surface the signal.

**Partial progress convention.** UNIX-standard behaviour: a `read(2)` or `write(2)` that transferred some bytes and was then interrupted returns the byte count as success, not an error.

**EINTR vs ERESTART.** Two signal return codes. EINTR surfaces to user space as errno EINTR. ERESTART causes the syscall layer to restart the syscall transparently, based on signal disposition.

**Acquire barrier.** A memory barrier on a load that prevents subsequent memory accesses from being reordered before the load.

**Release barrier.** A memory barrier on a store that prevents preceding memory accesses from being reordered after the store.

**Compare-and-swap (CAS).** An atomic operation that writes a new value only if the current value matches an expected one. Basis for lock-free state machines.

**Upgrade-promote-downgrade.** An sx pattern: acquire shared, detect need to write, try to upgrade to exclusive, write, downgrade back to shared.

**Coalescing.** Taskqueue property: redundant enqueues of the same task fold into a single pending state with an incremented counter, rather than being linked separately.

**Encapsulation layer.** A header (`myfirst_sync.h`) that names every synchronisation operation the driver performs, so the strategy can be changed in one place and understood at a glance.

**State flag.** A small integer in the softc that records whether a specific condition is in progress. Protected by the appropriate lock or atomic discipline.

**Cross-component handshake.** A coordination between multiple execution contexts (callout, task, user thread) using a state flag, cv, or atomic.



Chapter 15 ends here. Part 4 begins next.


## Reference: Reading `kern_sema.c` Line by Line

The implementation of `sema(9)` is short enough to read end-to-end. Doing so once cements the mental model of what the primitive actually does. The file is `/usr/src/sys/kern/kern_sema.c`, under two hundred lines. A narrated pass follows.

### `sema_init`

```c
void
sema_init(struct sema *sema, int value, const char *description)
{

        KASSERT((value >= 0), ("%s(): negative value\n", __func__));

        bzero(sema, sizeof(*sema));
        mtx_init(&sema->sema_mtx, description, "sema backing lock",
            MTX_DEF | MTX_NOWITNESS | MTX_QUIET);
        cv_init(&sema->sema_cv, description);
        sema->sema_value = value;

        CTR4(KTR_LOCK, "%s(%p, %d, \"%s\")", __func__, sema, value, description);
}
```

Six lines of logic. Assert the initial value is non-negative. Zero the structure. Initialise the internal mutex; note the flags `MTX_NOWITNESS | MTX_QUIET`, which tell `WITNESS` not to track the internal mutex (because the semaphore itself is what the user cares about, not its backing mutex). Initialise the internal cv. Set the counter.

The implication: a semaphore is literally a mutex plus a cv plus a counter, assembled into a small package. Understanding this assembly is understanding the primitive.

### `sema_destroy`

```c
void
sema_destroy(struct sema *sema)
{
        CTR3(KTR_LOCK, "%s(%p) \"%s\"", __func__, sema,
            cv_wmesg(&sema->sema_cv));

        KASSERT((sema->sema_waiters == 0), ("%s(): waiters\n", __func__));

        mtx_destroy(&sema->sema_mtx);
        cv_destroy(&sema->sema_cv);
}
```

Two lines of logic after the tracing. Assert no waiters exist. Destroy the internal mutex and cv. The assertion is what forces you to quiesce waiters before destroying; violating it panics a debug kernel.

### `_sema_post`

```c
void
_sema_post(struct sema *sema, const char *file, int line)
{

        mtx_lock(&sema->sema_mtx);
        sema->sema_value++;
        if (sema->sema_waiters && sema->sema_value > 0)
                cv_signal(&sema->sema_cv);

        CTR6(KTR_LOCK, "%s(%p) \"%s\" v = %d at %s:%d", __func__, sema,
            cv_wmesg(&sema->sema_cv), sema->sema_value, file, line);

        mtx_unlock(&sema->sema_mtx);
}
```

Three lines of logic. Lock, increment, signal one waiter if any. The signal is conditional on two things: the waiters count is non-zero (no need to signal if nobody is waiting), and the value is positive (signals with a zero value would wake a waiter who would immediately sleep again). The second condition is subtle; it protects against a situation where `sema_value` went positive and then became zero again between a post and the current post. In practice on simple usage the conditions are both true.

### `_sema_wait`

```c
void
_sema_wait(struct sema *sema, const char *file, int line)
{

        mtx_lock(&sema->sema_mtx);
        while (sema->sema_value == 0) {
                sema->sema_waiters++;
                cv_wait(&sema->sema_cv, &sema->sema_mtx);
                sema->sema_waiters--;
        }
        sema->sema_value--;

        CTR6(KTR_LOCK, "%s(%p) \"%s\" v = %d at %s:%d", __func__, sema,
            cv_wmesg(&sema->sema_cv), sema->sema_value, file, line);

        mtx_unlock(&sema->sema_mtx);
}
```

Four lines of logic. Lock. Loop while the value is zero: increment waiters, wait on the cv, decrement waiters. Once value is positive, decrement it and unlock.

Two observations.

The loop is what makes the primitive safe against spurious wakeups. `cv_wait` can return without `cv_signal` having been called. The loop re-checks the value every time, so a spurious wakeup just re-sleeps.

The wait uses `cv_wait`, not `cv_wait_sig`. This is what makes `sema_wait` non-interruptible. A signal to the caller does nothing. The loop continues until a real `sema_post` arrives.

### `_sema_timedwait`

```c
int
_sema_timedwait(struct sema *sema, int timo, const char *file, int line)
{
        int error;

        mtx_lock(&sema->sema_mtx);

        for (error = 0; sema->sema_value == 0 && error == 0;) {
                sema->sema_waiters++;
                error = cv_timedwait(&sema->sema_cv, &sema->sema_mtx, timo);
                sema->sema_waiters--;
        }
        if (sema->sema_value > 0) {
                sema->sema_value--;
                error = 0;
                /* ... tracing ... */
        } else {
                /* ... tracing ... */
        }

        mtx_unlock(&sema->sema_mtx);
        return (error);
}
```

Slightly more complex. The loop uses `cv_timedwait`, again not `cv_timedwait_sig`, so the timed wait is also not interruptible. The loop exits when the value is positive or when the error becomes non-zero (usually `EWOULDBLOCK`).

After the loop: if the value is positive, we claim it and return 0 (success). Otherwise, we return the error (`EWOULDBLOCK`). Note the cv error is preserved from the last loop iteration in the error case.

A subtlety the comment in the source calls out: a spurious wakeup resets the effective timeout interval because each iteration uses a fresh timo. This means the actual wait may be slightly longer than the caller requested, but never shorter. The `EWOULDBLOCK` return eventually fires.

### `_sema_trywait`

```c
int
_sema_trywait(struct sema *sema, const char *file, int line)
{
        int ret;

        mtx_lock(&sema->sema_mtx);

        if (sema->sema_value > 0) {
                sema->sema_value--;
                ret = 1;
        } else {
                ret = 0;
        }

        mtx_unlock(&sema->sema_mtx);
        return (ret);
}
```

Two lines of logic. Lock. If value is positive, decrement and return 1. Otherwise return 0. No blocking, no cv, no waiter accounting.

### `sema_value`

```c
int
sema_value(struct sema *sema)
{
        int ret;

        mtx_lock(&sema->sema_mtx);
        ret = sema->sema_value;
        mtx_unlock(&sema->sema_mtx);
        return (ret);
}
```

One line of logic. Returns the current value. The value can change immediately after the mutex is released, so the result is a snapshot, not a guarantee. Useful for diagnostics.

### Observations

Reading the whole file takes ten minutes. At the end you understand:

- Semaphores are built from a mutex and a cv.
- `sema_wait` is not signal-interruptible because it uses `cv_wait`.
- `sema_destroy` asserts no waiters, which is why you must quiesce before destroying.
- Coalescing does not exist (every post decrements the counter by one, no "pending" accounting).
- The primitive is simple; its contracts are precise.

This kind of reading is the shortest path to mastery of any kernel primitive. The `sema(9)` file is a particularly good starter because it is so short.



## Reference: Standardising Synchronization Primitives Across a Driver

As the driver accumulates more primitives, consistency matters more than cleverness.

### One Naming Convention

The Chapter 15 convention, which the reader can adopt or modify:

- **Mutex**: `sc->mtx`. Only one per driver. If the driver needs more than one, each has a purpose suffix: `sc->tx_mtx`, `sc->rx_mtx`.
- **Condition variable**: `sc-><purpose>_cv`. E.g., `data_cv`, `room_cv`.
- **Sx lock**: `sc-><purpose>_sx`. E.g., `cfg_sx`, `stats_cache_sx`.
- **Semaphore**: `sc-><purpose>_sema`. E.g., `writers_sema`.
- **Callout**: `sc-><purpose>_co`. E.g., `heartbeat_co`.
- **Task**: `sc-><purpose>_task`. E.g., `selwake_task`.
- **Timeout task**: `sc-><purpose>_delayed_task`. E.g., `reset_delayed_task`.
- **Atomic flag**: `sc-><purpose>` as an `int`. No suffix; the type tells the story. E.g., `is_attached`, `recovery_in_progress`.
- **State flag under mutex**: same as atomic; the comment in the softc names the lock.

### One Init/Destroy Pattern

Each primitive has a canonical init and destroy. The order in attach mirrors the reverse in detach.

Attach order:
1. Mutex.
2. Cvs.
3. Sx locks.
4. Semaphores.
5. Taskqueue.
6. Callouts.
7. Tasks.
8. Atomic flags (no init required; initialised by softc zeroing).

Detach order (roughly reverse):
1. Clear atomic flag.
2. Broadcast cvs.
3. Release mutex.
4. Drain callouts.
5. Drain tasks.
6. Free taskqueue.
7. Destroy semaphores.
8. Destroy sxs.
9. Destroy cvs.
10. Destroy mutex.

The rule of thumb: destroy in the opposite order to init, and drain anything that can still fire before destroying what it touches.

### One Encapsulation Pattern

The `myfirst_sync.h` pattern from Section 6 scales. Every primitive has a wrapper. Every wrapper is named for what it does, not for the primitive it wraps.

### One LOCKING.md Template

A `LOCKING.md` section per primitive. Each section names:

- The primitive.
- Its purpose.
- Its lifetime.
- Its contract with other primitives (lock order, ownership, interaction with the atomic flag).

A new primitive added to the driver adds a new section. A modified primitive changes its existing section. The document is always current.

### Why Standardise

The benefits are the same as in Chapter 14. Reduced cognitive load. Fewer mistakes. Easier review. Easier handoff. The costs are small and one-time.



## Reference: When Each Primitive Is Wrong

Synchronisation primitives are tools. Every tool has misuses. A short list of anti-patterns to recognise.

### Mutex Misuses

- **Holding a mutex across a sleep.** Prevents other threads from making progress; may cause deadlock if the sleep depends on state another thread needs the mutex for.
- **Nesting mutexes in inconsistent order.** Creates a lock-order cycle; `WITNESS` catches it.
- **Using a mutex where an atomic would do.** For a single-bit flag checked often, a mutex is heavier than needed.
- **Missing `mtx_assert` in helpers that require the mutex.** Without the assertion, the helper can be called in a context where the mutex is not held; the bug may be silent.

### Cv Misuses

- **Using `cv_wait` in a syscall context.** Cannot be interrupted by signals; makes the syscall unresponsive.
- **Not re-checking the predicate after wake-up.** Spurious wakeups are allowed; code that assumes a wakeup means the predicate is true is buggy.
- **Signalling without holding the mutex.** Usually allowed by the API but typically unwise; the waiter may miss the signal if the timing is unlucky.
- **Using `cv_signal` where `cv_broadcast` is needed.** A detach path that wakes only one waiter leaves others blocked.

### Sx Misuses

- **Using sx where a mutex would do.** Sx has higher overhead than a mutex in the uncontended case; if there is no shared-access benefit, mutex is simpler.
- **Forgetting that sx can sleep.** Sx cannot be acquired from a non-sleeping context. Callouts initialised with `callout_init_mtx(, &mtx, 0)` are fine; filter interrupts are not. (Historical note: the older `CALLOUT_MPSAFE` flag named the same distinction. Chapter 13 walks through its deprecation.)
- **Using `sx_try_upgrade` without the fallback.** A naive upgrade that does not handle failure races against another upgrader.

### Sema Misuses

- **Expecting signal interruption.** `sema_wait` does not interrupt.
- **Expecting priority inheritance.** `sema` does not raise the poster's priority.
- **Destroying with waiters.** Panics a debug kernel, silently corrupts a production kernel.
- **Forgetting a post on an error path.** Leaks a slot; eventually the semaphore drains to zero and all waiters block.

### Atomic Misuses

- **Using plain atomics where acquire/release is needed.** Correct on x86, broken on arm64. Always think about memory ordering.
- **Protecting a complex invariant with a single atomic.** If the invariant involves multiple fields, atomics alone are insufficient; a lock is needed.
- **Using CAS where a simple load-store would do.** Wastes an atomic instruction.

### Pattern Misuses

- **Rolling your own semaphore from mutex-plus-counter-plus-cv.** The kernel's `sema(9)` already does this; reinventing it creates maintenance debt.
- **Rolling your own read-write lock from mutex-plus-counter.** `sx(9)` does this; reinventing it creates maintenance debt.
- **Rolling your own encapsulation in the driver source.** Put it in a header (`myfirst_sync.h`); do not duplicate it inline.

Recognising anti-patterns is half of good synchronisation. The other half is choosing the right primitive in the first place, which the main body of the chapter has covered.



## Reference: Observability Cheat Sheet

### Sysctl Knobs Your Driver Should Expose

For Chapter 15, add these to the driver's sysctl tree (at `dev.myfirst.N.stats.*` or `dev.myfirst.N.*` as appropriate):

- `writers_limit`: current writer cap.
- `stats.writers_sema_value`: snapshot of the semaphore value.
- `stats.writers_trywait_failures`: count of non-blocking writers refused.
- `stats.stats_cache_refreshes`: count of cache refreshes.
- `stats.recovery_task_runs`: count of recovery invocations.
- `stats.is_attached`: current atomic flag value.

These read-only counters give an operator a window into the driver's synchronisation behaviour without a debugger.

### DTrace Probes

**Count cv signals per cv:**

```text
dtrace -n 'fbt::cv_signal:entry { @[stringof(args[0]->cv_description)] = count(); }'
```

**Count sema waits and posts:**

```text
dtrace -n '
  fbt::_sema_wait:entry { @["wait"] = count(); }
  fbt::_sema_post:entry { @["post"] = count(); }
'
```

**Measure sema wait latency:**

```text
dtrace -n '
  fbt::_sema_wait:entry { self->t = timestamp; }
  fbt::_sema_wait:return /self->t/ {
        @ = quantize(timestamp - self->t);
        self->t = 0;
  }
'
```

Useful for understanding how long writers block on the writer-cap in practice.

**Measure sx contention:**

```text
dtrace -n '
  lockstat:::sx-block-enter /arg0 == (uintptr_t)&sc_addr/ {
        @ = count();
  }
'
```

Replace `sc_addr` with the actual address of your sx. Shows how often the sx blocked.

**Observe taskqueue_run_locked for recovery:**

```text
dtrace -n 'fbt::myfirst_recovery_task:entry { printf("recovery at %Y", walltimestamp); }'
```

Prints a timestamp every time the recovery task fires.

### procstat

`procstat -t | grep myfirst`: shows taskqueue worker threads and their state.

`procstat -kk <pid>`: kernel stack of a specific thread. Useful when something is stuck.

### ps

`ps ax | grep taskq`: list every taskqueue worker by name.

### ddb

`db> show witness`: dump the WITNESS lock graph.

`db> show locks`: list currently held locks.

`db> show sleepchain <tid>`: walk a sleep chain to find deadlocks.



## Reference: A Worked Stage-by-Stage Diff Summary

A compact summary of the driver diff from Chapter 14 Stage 4 to Chapter 15 Stage 4, for readers who want to see the whole change at a glance.

### Stage 1 Diff (v0.8 -> v0.8+writers_sema)

**Softc additions:**

```c
struct sema     writers_sema;
int             writers_limit;
int             writers_trywait_failures;
int             writers_inflight;   /* atomic int; drain counter */
```

**Attach additions:**

```c
sema_init(&sc->writers_sema, 4, "myfirst writers");
sc->writers_limit = 4;
sc->writers_trywait_failures = 0;
sc->writers_inflight = 0;
```

**Detach additions (after is_attached=0 and all cv broadcasts):**

```c
sema_destroy(&sc->writers_sema);
```

**New sysctl handler:** `myfirst_sysctl_writers_limit`.

**Write-path changes:** sema acquisition at entry, release at exit, O_NONBLOCK via sema_trywait.

### Stage 2 Diff (+stats_cache_sx)

**Softc additions:**

```c
struct sx       stats_cache_sx;
uint64_t        stats_cache_bytes_10s;
uint64_t        stats_cache_last_refresh_ticks;
```

**Attach additions:**

```c
sx_init(&sc->stats_cache_sx, "myfirst stats cache");
sc->stats_cache_bytes_10s = 0;
sc->stats_cache_last_refresh_ticks = 0;
```

**Detach additions (after mutex destroy):**

```c
sx_destroy(&sc->stats_cache_sx);
```

**New helper:** `myfirst_stats_cache_refresh`.

**New sysctl handler:** `myfirst_sysctl_stats_cached`.

### Stage 3 Diff (EINTR/ERESTART + partial-progress)

**No softc changes.**

**Wait-helper refactor:** `MYFIRST_WAIT_PARTIAL` sentinel; explicit switch on error codes from cv waits.

**Caller changes:** explicit checks for the sentinel.

### Stage 4 Diff (coordination + encapsulation)

**Softc additions:**

```c
int             recovery_in_progress;
struct task     recovery_task;
int             recovery_task_runs;
```

**Attach additions:**

```c
TASK_INIT(&sc->recovery_task, 0, myfirst_recovery_task, sc);
sc->recovery_in_progress = 0;
sc->recovery_task_runs = 0;
```

**Detach additions:**

```c
taskqueue_drain(sc->tq, &sc->recovery_task);
```

**Atomic flag conversions:** `is_attached` reads in handlers and callbacks become `atomic_load_acq_int`; the detach write becomes `atomic_store_rel_int`.

**New header:** `myfirst_sync.h` with inline wrappers.

**Source edits:** every primitive-specific call in the main source becomes a wrapper call.

**Watchdog refactor:** enqueue recovery task on stall, guarded by the `recovery_in_progress` flag.

**Version bump:** `MYFIRST_VERSION "0.9-coordination"`.

### Total Lines Added

Rough count across the four stages:

- Softc: ~10 fields.
- Attach: ~15 lines.
- Detach: ~10 lines.
- New functions: ~80 lines (sysctl handlers, recovery_task, helpers).
- Modified functions: ~20 line edits.
- Header file: ~150 lines.

Net addition to the driver: approximately 300 lines. Compare to the approximately 100 lines Chapter 14 added and the 400 lines Chapter 13 added. The Chapter 15 additions are modest in volume; large in what they enable.



## Reference: The Chapter 15 Driver Lifecycle

A summary of the lifecycle with the Chapter 15 additions explicit.

### Attach Sequence

1. `mtx_init(&sc->mtx, ...)`.
2. `cv_init(&sc->data_cv, ...)`, `cv_init(&sc->room_cv, ...)`.
3. `sx_init(&sc->cfg_sx, ...)`.
4. `sx_init(&sc->stats_cache_sx, ...)`.
5. `sema_init(&sc->writers_sema, 4, ...)`.
6. `sc->tq = taskqueue_create(...)`; `taskqueue_start_threads(...)`.
7. `callout_init_mtx(&sc->heartbeat_co, ...)`, two more.
8. `TASK_INIT(&sc->selwake_task, ...)`, three more (including recovery_task).
9. `TIMEOUT_TASK_INIT(sc->tq, &sc->reset_delayed_task, ...)`.
10. Softc fields initialised.
11. `sc->bytes_read = counter_u64_alloc(M_WAITOK)`; same for bytes_written.
12. `cbuf_init(&sc->cb, ...)`.
13. `make_dev_s(...)` for the cdev.
14. Sysctl tree set up.
15. `sc->is_attached = 1` (initial store is not strictly atomic-ordered because no reader can see the softc until it is attached).

### Runtime

- User threads enter/leave via open/close/read/write.
- Callouts fire periodically.
- Tasks fire on enqueue.
- Watchdog detects stalls, enqueues recovery task.
- Atomic flag read via `myfirst_sync_is_attached` in entry checks.

### Detach Sequence

1. `myfirst_detach` is called.
2. Check `active_fhs > 0`; return `EBUSY` if so.
3. `atomic_store_rel_int(&sc->is_attached, 0)`.
4. `cv_broadcast(&sc->data_cv)`, `cv_broadcast(&sc->room_cv)`.
5. Release `sc->mtx`.
6. `callout_drain` three times.
7. `taskqueue_drain` three times (selwake, bulk_writer, recovery).
8. `taskqueue_drain_timeout` once (reset_delayed).
9. `seldrain` twice.
10. `taskqueue_free(sc->tq)`.
11. `sema_destroy(&sc->writers_sema)`.
12. `destroy_dev` twice.
13. `sysctl_ctx_free`.
14. `cbuf_destroy`, `counter_u64_free` twice.
15. `sx_destroy(&sc->stats_cache_sx)`.
16. `cv_destroy` twice.
17. `sx_destroy(&sc->cfg_sx)`.
18. `mtx_destroy(&sc->mtx)`.

### Notes on the Sequence

- Every primitive init in attach has a matching destroy in detach, in reverse order.
- Every drain happens before the thing drained is freed.
- The atomic flag is the very first step after the `active_fhs` check, so every subsequent observer sees the shutdown.
- The taskqueue is freed before the semaphore is destroyed because a task might (in some extended design) wait on the semaphore.

Memorising this lifecycle is the single most useful thing a reader can do with Part 3.



## Reference: Costs and Comparisons

A concise table of the synchronisation primitives' costs.

| Primitive | Uncontended cost | Contended cost | Sleep? |
|---|---|---|---|
| `atomic_load_acq_int` | ~1 ns on amd64 | ~1 ns (same) | No |
| `atomic_fetchadd_int` | ~10 ns | ~100 ns | No |
| `mtx_lock` (uncontended) | ~20 ns | microseconds | Slow path sleeps |
| `cv_wait_sig` | n/a | full scheduler wakeup | Yes |
| `sx_slock` | ~30 ns | microseconds | Slow path sleeps |
| `sx_xlock` | ~30 ns | microseconds | Slow path sleeps |
| `sx_try_upgrade` | ~30 ns | n/a (fails fast) | No |
| `sema_wait` | ~40 ns | wakeup latency | Yes |
| `sema_post` | ~30 ns | ~100 ns | No |
| `callout_reset` | ~100 ns | n/a | No |
| `taskqueue_enqueue` | ~50 ns | wakeup latency | No |

The figures above are order-of-magnitude estimates on typical FreeBSD 14.3 amd64 hardware, and the microsecond entries in the contended column correspond to low-microsecond wakeup latencies on that same class of machine. Actual numbers depend on cache state, contention, and system load, and can move by a factor of two or more across CPU generations. Use this table to decide where to optimise; a call taking hundreds of nanoseconds is not a bottleneck on a path that runs once per syscall. See Appendix F for a reproducible benchmark of these figures on your own hardware.



## Reference: A Worked Synchronization Walk

For a fully concrete example, here is the complete control-flow of a blocking `read(2)` in the Chapter 15 Stage 4 driver, from syscall entry to data delivery.

1. User calls `read(fd, buf, 4096)`.
2. Kernel's VFS layer routes to `myfirst_read`.
3. `myfirst_read` calls `devfs_get_cdevpriv` to get the fh.
4. `myfirst_read` calls `myfirst_sync_is_attached(sc)`:
   - Expands to `atomic_load_acq_int(&sc->is_attached)`.
   - Returns 1 (attached).
5. Loop enters: `while (uio->uio_resid > 0)`.
6. `myfirst_sync_lock(sc)`:
   - Expands to `mtx_lock(&sc->mtx)`.
   - Acquires the mutex.
7. `myfirst_wait_data(sc, ioflag, nbefore, uio)`:
   - `while (cbuf_used == 0)`: buffer is empty.
   - Not partial (nbefore == uio_resid).
   - Not IO_NDELAY.
   - `read_timeout_ms` is 0, so uses `cv_wait_sig`.
   - `cv_wait_sig(&sc->data_cv, &sc->mtx)`:
     - Releases the mutex.
     - Thread sleeps on the cv.
   - Time passes. A writer (or the tick_source) calls `cv_signal(&sc->data_cv)`.
   - Thread wakes, `cv_wait_sig` re-acquires the mutex, returns 0.
   - `!sc->is_attached`: false.
   - Loop re-iterates: `cbuf_used` is now > 0.
   - Exits the loop; returns 0.
8. `myfirst_buf_read(sc, bounce, take)`:
   - Calls `cbuf_read(&sc->cb, bounce, take)`.
   - Copies data into the bounce buffer.
   - Increments `bytes_read` counter.
9. `myfirst_sync_unlock(sc)`.
10. `cv_signal(&sc->room_cv)`: wake a blocked writer if any.
11. `selwakeup(&sc->wsel)`: wake any poller waiting for write.
12. `uiomove(bounce, got, uio)`: copy from kernel to user space.
13. Loop continues: check `uio->uio_resid > 0`; eventually falls out.
14. Returns 0 to the syscall layer.
15. Syscall layer returns the number of bytes copied to user.

Every primitive in Chapter 15 is visible in the walk:

- The atomic flag (step 4).
- The mutex (step 6, 7, 9).
- The cv wait with signal handling (step 7's cv_wait_sig).
- The counter (step 8's counter_u64_add).
- The cv signal (step 10).
- The selwakeup (step 11, done outside the mutex per discipline).

A walk like this is a useful cross-check. Every primitive in the driver's vocabulary is exercised on the read path. If you can narrate the walk from memory, the synchronisation is internalised.



## Reference: A Minimal Working Template

For copy-and-adapt convenience, a template that compiles and demonstrates Chapter 15's core additions in the smallest possible form. Every element has been introduced in the chapter; the template assembles them.

```c
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/mutex.h>
#include <sys/lock.h>
#include <sys/sx.h>
#include <sys/sema.h>
#include <sys/taskqueue.h>
#include <sys/priority.h>

struct template_softc {
        device_t           dev;
        struct mtx         mtx;
        struct sx          stats_sx;
        struct sema        admission_sema;
        struct taskqueue  *tq;
        struct task        work_task;
        int                is_attached;
        int                work_in_progress;
};

static void
template_work_task(void *arg, int pending)
{
        struct template_softc *sc = arg;
        mtx_lock(&sc->mtx);
        /* Work under mutex if needed. */
        sc->work_in_progress = 0;
        mtx_unlock(&sc->mtx);
        /* Unlocked work here. */
}

static int
template_attach(device_t dev)
{
        struct template_softc *sc = device_get_softc(dev);
        int error;

        sc->dev = dev;
        mtx_init(&sc->mtx, device_get_nameunit(dev), "template", MTX_DEF);
        sx_init(&sc->stats_sx, "template stats");
        sema_init(&sc->admission_sema, 4, "template admission");

        sc->tq = taskqueue_create("template taskq", M_WAITOK,
            taskqueue_thread_enqueue, &sc->tq);
        if (sc->tq == NULL) { error = ENOMEM; goto fail_sema; }
        error = taskqueue_start_threads(&sc->tq, 1, PWAIT,
            "%s taskq", device_get_nameunit(dev));
        if (error != 0) goto fail_tq;

        TASK_INIT(&sc->work_task, 0, template_work_task, sc);

        atomic_store_rel_int(&sc->is_attached, 1);
        return (0);

fail_tq:
        taskqueue_free(sc->tq);
fail_sema:
        sema_destroy(&sc->admission_sema);
        sx_destroy(&sc->stats_sx);
        mtx_destroy(&sc->mtx);
        return (error);
}

static int
template_detach(device_t dev)
{
        struct template_softc *sc = device_get_softc(dev);

        atomic_store_rel_int(&sc->is_attached, 0);

        taskqueue_drain(sc->tq, &sc->work_task);
        taskqueue_free(sc->tq);
        sema_destroy(&sc->admission_sema);
        sx_destroy(&sc->stats_sx);
        mtx_destroy(&sc->mtx);
        return (0);
}

/* Entry on the hot path: */
static int
template_hotpath_enter(struct template_softc *sc)
{
        sema_wait(&sc->admission_sema);
        if (!atomic_load_acq_int(&sc->is_attached)) {
                sema_post(&sc->admission_sema);
                return (ENXIO);
        }
        return (0);
}

static void
template_hotpath_leave(struct template_softc *sc)
{
        sema_post(&sc->admission_sema);
}
```

The template is not a complete driver. It shows the shape of the primitives the chapter introduced. A real driver with this template plus the rest of the Chapter 14 and earlier patterns would be a fully functional synchronised device driver.



## Reference: Comparison With POSIX User-Space Synchronization

Many readers come to FreeBSD kernel driver work from user-space systems programming. A short comparison clarifies the mapping.

| Concept | POSIX user-space | FreeBSD kernel |
|---|---|---|
| Mutex | `pthread_mutex_t` | `struct mtx` |
| Condition variable | `pthread_cond_t` | `struct cv` |
| Read-write lock | `pthread_rwlock_t` | `struct sx` |
| Semaphore | `sem_t` (or `sem_open`) | `struct sema` |
| Thread creation | `pthread_create` | `kproc_create`, `kthread_add` |
| Deferred work | No direct analogue; common to roll your own | `struct task` + `struct taskqueue` |
| Periodic execution | `timer_create` + `signal` | `struct callout` |
| Atomic operations | `<stdatomic.h>` or `__atomic_*` | `atomic(9)` |

The primitives are similar in shape but different in detail. Key differences:

- Kernel mutexes have priority inheritance; POSIX mutexes do only with the `PRIO_INHERIT` attribute.
- Kernel cvs have no name; POSIX cvs are anonymous too, so parity here.
- Kernel sx locks are more flexible than pthread_rwlock (try_upgrade, downgrade).
- Kernel semaphores are not signal-interruptible; POSIX semaphores (some variants) are.
- Kernel taskqueues have no direct POSIX analogue; POSIX thread pools are roll-your-own.
- Kernel atomics are more comprehensive (more operations, better barrier control) than older C11 atomics.

A reader comfortable with POSIX synchronisation will find the kernel primitives intuitive with minor adjustments. The main adjustment is the context awareness: kernel code cannot assume the ability to block.



## Reference: A Worked Pattern Catalog

Ten synchronisation patterns that recur in real drivers. Each is a variation on the primitives Chapter 15 introduced.

### Pattern 1: Producer/Consumer With Bounded Queue

Chapter 14's cbuf plus Chapter 12's cvs already implement this. The producer writes, the consumer reads, the mutex protects the queue, the cvs signal empty-to-nonempty and full-to-nonfull transitions.

### Pattern 2: Completion Semaphore

A submitter initialises a semaphore to 0 and waits. A completion handler posts. The submitter unblocks. Used for request-reply patterns. See `/usr/src/sys/dev/hyperv/storvsc/hv_storvsc_drv_freebsd.c`.

### Pattern 3: Admission Control

A semaphore initialised to N. Each participant `sema_wait`s on entry and `sema_post`s on exit. Chapter 15 Stage 1 uses this pattern.

### Pattern 4: Upgrade-Promote-Downgrade Read Cache

An sx lock with a staleness-based refresh. Readers take shared; on stale they try_upgrade, refresh, downgrade. Chapter 15 Stage 2 uses this pattern.

### Pattern 5: Atomic Flag Coordination

An atomic flag read by many contexts, written by one. Use `atomic_load_acq` for reads, `atomic_store_rel` for writes. Chapter 15 Stage 4 uses this for `is_attached`.

### Pattern 6: At-Most-One State Flag

A state flag protected by a lock or CAS. The "start of operation" path sets the flag; the "end of operation" path clears it. Chapter 15 Stage 4 uses this for `recovery_in_progress`.

### Pattern 7: Signal-Interruptible Bounded Wait

`cv_timedwait_sig` with explicit EINTR/ERESTART/EWOULDBLOCK handling. Chapter 15 Stage 3 refines this pattern.

### Pattern 8: Periodic Refresh via Callout

A callout periodically invokes a refresh function. The refresh holds a lock briefly. Simpler than Pattern 4 when the refresh interval is fixed.

### Pattern 9: Deferred Teardown via Reference Count

Object has a reference count. "Free" decrements; actual free happens when the count reaches zero. Atomic decrement ensures correctness.

### Pattern 10: Cross-Subsystem Handshake

Two subsystems coordinate via a shared state flag plus a cv or sema. One signals "my part is done"; the other waits. Useful for staged shutdown.

Knowing these patterns makes reading real driver source faster. Every Chapter 15 primitive is a building block for one or more of these patterns, and every real driver picks the patterns its workload needs.



## Reference: A Glossary of Part 3 Terms

For final reference, a consolidated glossary spanning Chapters 11 through 15.

**Atomic operation.** A read, write, or read-modify-write primitive that executes without possibility of concurrent interference at the hardware level.

**Barrier.** A directive that prevents the compiler or CPU from reordering memory operations past a specific point.

**Blocking wait.** A synchronisation operation that puts the caller to sleep until a condition is met or a timeout fires.

**Broadcast.** Wake every thread blocked on a cv or sema.

**Callout.** A deferred function scheduled to run at a specific tick count in the future.

**Coalescing.** Folding multiple requests into a single operation (e.g., task enqueues into `ta_pending`).

**Condition variable.** A primitive that allows a thread to sleep until another thread signals a state change.

**Context.** The execution environment of a code path (syscall, callout, task, interrupt, etc.) with its own rules about what operations are safe.

**Counter.** A per-CPU accumulator primitive (`counter(9)`) used for lock-free statistics.

**Drain.** Wait until a pending operation is no longer pending and not currently executing.

**Enqueue.** Add a work item to a queue. Typically triggers a wakeup of the consumer.

**Epoch.** A synchronisation mechanism that allows lock-free reads of shared structures; writers defer reclamation via `epoch_call`.

**Exclusive lock.** A lock held by at most one thread; writers use exclusive mode.

**Filter interrupt.** An interrupt handler that runs in hardware context with severe restrictions on what it can do.

**Grouptaskqueue.** A scalable taskqueue variant with per-CPU worker queues; used by high-rate network drivers.

**Interruptible wait.** A blocking wait that can be woken by a signal, returning EINTR or ERESTART.

**Memory ordering.** Rules about the visibility and order of memory accesses across CPUs.

**Mutex.** A primitive ensuring mutual exclusion; at most one thread inside the lock at a time.

**Partial progress.** Bytes already copied on a read or write when an interrupt or timeout fires; conventionally returned as success with a short count.

**Priority inheritance.** A scheduler mechanism where a high-priority waiter temporarily raises the priority of the current holder.

**Release barrier.** A barrier on a store that ensures preceding accesses cannot be reordered after the store.

**Semaphore.** A primitive with a non-negative counter; `post` increments, `wait` decrements (blocking if zero).

**Shared lock.** A lock held by many threads at once; readers use shared mode.

**Spinlock.** A lock whose slow path busy-waits rather than sleeping. `MTX_SPIN` mutex.

**Sx lock.** A shared/exclusive lock; FreeBSD's read-write lock primitive.

**Task.** A deferred-work item with a callback and context, submitted to a taskqueue.

**Taskqueue.** A queue of pending tasks serviced by one or more worker threads.

**Timeout.** A duration beyond which a wait should abandon and return EWOULDBLOCK.

**Timeout task.** A task scheduled for a specific future moment via `taskqueue_enqueue_timeout`.

**Upgrade.** Promoting a shared sx lock to exclusive without releasing it (`sx_try_upgrade`).

**Wakeup.** Waking a thread blocked on a cv, a sema, or a sleep queue.



Part 3 ends here. Part 4 begins with Chapter 16, *Hardware Basics and Newbus*.


## Reference: A Worked Debugging Scenario

A narrative walkthrough of a realistic synchronisation bug. Imagine you inherit a driver written by a colleague. The colleague is on holiday. Users report "under heavy load, the driver panics on detach with a stack trace ending in `selwakeup`".

This section walks through how you would diagnose and fix the problem using the Chapter 15 toolkit.

### Step 1: Reproduce

First priority: get a reliable reproduction. Without one, fixes are guesses.

Start by reading the bug report for specifics. "Heavy load" plus "panics on detach" is a strong hint that the race is between a running worker and the detach path. A stack trace ending in `selwakeup` suggests the panic is inside the selinfo code.

Write a minimal stress script that reproduces the scenario:

```text
#!/bin/sh
kldload ./myfirst.ko
sysctl dev.myfirst.0.tick_source_interval_ms=1
(while :; do dd if=/dev/myfirst of=/dev/null bs=1 count=100 2>/dev/null; done) &
READER=$!
sleep 5
kldunload myfirst
kill -TERM $READER
```

Run the script in a loop until the panic fires. On a debug kernel, this is the kind of bug that panics within seconds. On a production kernel, it may take longer.

### Step 2: Capture the Stack

A debug kernel has `KDB` enabled. After the panic, you land in the in-kernel debugger. Commands:

- `bt`: stack trace of the panicking thread.
- `show locks`: locks held at the moment of the panic.
- `show witness`: the WITNESS state.

Record everything. On a production crash, a kernel dump is collected; retrieve it with `crashinfo` and analyse offline with `kgdb`.

A typical stack for this bug:

```text
panic: selwakeup: freed selinfo
kdb_enter
vpanic
panic
selwakeup
myfirst_selwake_task
taskqueue_run_locked
taskqueue_thread_loop
fork_exit
```

The panic is in `selwakeup`, called from `myfirst_selwake_task`, called from the taskqueue worker. The `selwakeup`'s assertion says the selinfo it was given is freed.

### Step 3: Identify the Race

The bug is visible in the stack. The task callback called `selwakeup(&sc->rsel)`, but `sc->rsel` had been drained or freed. Two possible causes:

A. The detach path `seldrain`'d the selinfo before draining the task. Then the task fired after `seldrain`, against the freed state.
B. The detach path freed the softc (containing rsel) before draining the task. Then the task fired against freed memory.

Both are ordering violations. Look at the actual detach code in the driver:

```c
/* Current order in the buggy driver: */
seldrain(&sc->rsel);
seldrain(&sc->wsel);
taskqueue_drain(sc->tq, &sc->selwake_task);
```

The `seldrain` is before the `taskqueue_drain`. That is exactly cause A. The fix is to swap the order.

### Step 4: Apply the Fix

Change the detach to:

```c
taskqueue_drain(sc->tq, &sc->selwake_task);
seldrain(&sc->rsel);
seldrain(&sc->wsel);
```

The task drain now guarantees the task is not running. Only then does `seldrain` proceed.

But wait. The task could still be enqueued from a running callout. Check: is the callout drained before the task drain? Read the code again:

```c
/* Full order after fix: */
MYFIRST_CO_DRAIN(&sc->heartbeat_co);
MYFIRST_CO_DRAIN(&sc->watchdog_co);
MYFIRST_CO_DRAIN(&sc->tick_source_co);
taskqueue_drain(sc->tq, &sc->selwake_task);
seldrain(&sc->rsel);
seldrain(&sc->wsel);
```

Callouts drained, then task drained, then sel drained. This is the correct order.

### Step 5: Verify

Run the reproduction script again with the fix. The panic should no longer fire. Run it 100 times to gain confidence. On a debug kernel the race surfaces quickly; 100 clean runs is strong evidence the fix is correct.

Add a regression test that exercises this specific scenario. The test should be part of the driver's test kit so the bug does not return.

### Step 6: Document

Update `LOCKING.md` with a comment explaining why the order is what it is. A future maintainer who considers reordering the drains for some reason will see the comment and reconsider.

### Takeaways

- The bug was visible in the stack trace; the skill was recognising what the trace meant.
- The fix was one line (reorder two calls); the diagnosis was the work.
- The debug kernel made the bug reproducible; without it, the bug would be intermittent and mysterious.
- The test kit prevents regression; without it, a future refactor could reintroduce the bug silently.

Chapter 14 already taught this specific ordering rule. A production driver written by a colleague who had not internalised Chapter 14 could easily have this bug. The chapter's discipline and the Chapter 15 testing chapter together are what keep this bug out of your own code.

This is a short, contrived scenario. Real bugs are subtler. The same methodology applies: reproduce, capture, identify, fix, verify, document. The primitives and the discipline from Part 3 are the toolbox for the "identify" step, which is usually the hardest.



## Reference: Reading a Real Driver's Synchronization

For a concrete, exam-style exercise: pick one Ethernet driver in `/usr/src/sys/dev/` and walk through its synchronisation vocabulary. This section walks through `/usr/src/sys/dev/ale/if_ale.c` briefly as a template for the exercise.

The `ale(4)` driver is a 10/100/1000 Ethernet driver for the Atheros AR8121/AR8113/AR8114. It is not large (a few thousand lines) and has a clean structure.

### Primitives It Uses

Open the file and search for the primitives.

```text
$ grep -c 'mtx_init\|mtx_lock\|mtx_unlock' /usr/src/sys/dev/ale/if_ale.c
```

The driver uses one mutex (`sc->ale_mtx`). Uniform acquisition via `ALE_LOCK(sc)` and `ALE_UNLOCK(sc)` macros.

It uses callouts: `sc->ale_tick_ch` for periodic link-state polling.

It uses a taskqueue: `sc->ale_tq`, created with `taskqueue_create_fast`, started with `taskqueue_start_threads(..., 1, PI_NET, ...)`. The fast variant (spin-mutex-backed) is used because the interrupt filter enqueues onto it.

It uses a task on the queue: `sc->ale_int_task` for interrupt post-processing.

It does not use `sema` or `sx`. The driver's invariants fit a single mutex.

It uses atomics: several `atomic_set_32` and `atomic_clear_32` calls on hardware registers (via `CSR_WRITE_4` and similar). These are for hardware register manipulation, not driver-level coordination.

### Patterns It Demonstrates

**Filter-plus-task interrupt split.** `ale_intr` is the filter, which masks the IRQ at the hardware and enqueues the task. `ale_int_task` is the task, which processes the interrupt work in thread context.

**Callout for link polling.** `ale_tick` is a periodic callout that re-arms itself, used for link-state polling.

**Standard detach ordering.** `ale_detach` drains callouts, drains tasks, frees the taskqueue, destroys the mutex. Same pattern as `myfirst`.

### What It Does Not Demonstrate

- No sx lock. Configuration is protected by the single mutex.
- No semaphore. No bounded admission.
- No epoch. The driver does not touch network state directly from unusual contexts.
- No `sx_try_upgrade`. No read-mostly cache.

### Takeaways

The `ale(4)` driver uses the subset of Part 3 primitives that its workload needs. A driver that needed an sx lock or a sema would add it; `ale(4)` does not, so it does not.

Reading one real driver like this is worth more than reading the chapter twice. Pick a driver, read it for 30 minutes, write down what primitives it uses and why.

Do the same exercise with a larger driver (`bge(4)`, `iwm(4)`, `mlx5(4)` are good candidates). Notice how the vocabulary scales. A driver with more state needs more primitives; a driver with simpler state uses fewer.

The `myfirst` driver at the end of Chapter 15 uses every primitive Part 3 introduced. Most real drivers use a subset. Both are valid; the choice depends on the workload.



## Reference: The Full `myfirst_sync.h` Design

The companion source under `examples/part-03/ch15-more-synchronization/stage4-final/` includes the complete `myfirst_sync.h`. For reference, here is a full version that can be used as a template.

```c
/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Edson Brandi
 *
 * myfirst_sync.h: the named synchronisation vocabulary of the
 * myfirst driver.
 *
 * Every primitive the driver uses has a wrapper here. The main
 * source calls these wrappers; the wrappers are inlined away, so
 * the runtime cost is zero. The benefit is a readable,
 * centralised, and easily-changeable synchronisation strategy.
 *
 * This file depends on the definition of `struct myfirst_softc`
 * in myfirst.c, which must be included before this header.
 */

#ifndef MYFIRST_SYNC_H
#define MYFIRST_SYNC_H

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/sx.h>
#include <sys/sema.h>
#include <sys/condvar.h>

/*
 * Data-path mutex. Single per-softc. Protects cbuf, counters, and
 * most of the per-softc state.
 */
static __inline void
myfirst_sync_lock(struct myfirst_softc *sc)
{
        mtx_lock(&sc->mtx);
}

static __inline void
myfirst_sync_unlock(struct myfirst_softc *sc)
{
        mtx_unlock(&sc->mtx);
}

static __inline void
myfirst_sync_assert_locked(struct myfirst_softc *sc)
{
        mtx_assert(&sc->mtx, MA_OWNED);
}

/*
 * Configuration sx. Protects the myfirst_config structure. Read
 * paths take shared; sysctl writers take exclusive.
 */
static __inline void
myfirst_sync_cfg_read_begin(struct myfirst_softc *sc)
{
        sx_slock(&sc->cfg_sx);
}

static __inline void
myfirst_sync_cfg_read_end(struct myfirst_softc *sc)
{
        sx_sunlock(&sc->cfg_sx);
}

static __inline void
myfirst_sync_cfg_write_begin(struct myfirst_softc *sc)
{
        sx_xlock(&sc->cfg_sx);
}

static __inline void
myfirst_sync_cfg_write_end(struct myfirst_softc *sc)
{
        sx_xunlock(&sc->cfg_sx);
}

/*
 * Writer-cap semaphore. Caps concurrent writers at
 * sc->writers_limit. Returns 0 on success, EAGAIN if O_NONBLOCK
 * and semaphore is exhausted, ENXIO if detach happened while
 * blocked.
 */
static __inline int
myfirst_sync_writer_enter(struct myfirst_softc *sc, int ioflag)
{
        if (ioflag & IO_NDELAY) {
                if (!sema_trywait(&sc->writers_sema)) {
                        mtx_lock(&sc->mtx);
                        sc->writers_trywait_failures++;
                        mtx_unlock(&sc->mtx);
                        return (EAGAIN);
                }
        } else {
                sema_wait(&sc->writers_sema);
        }
        if (!atomic_load_acq_int(&sc->is_attached)) {
                sema_post(&sc->writers_sema);
                return (ENXIO);
        }
        return (0);
}

static __inline void
myfirst_sync_writer_leave(struct myfirst_softc *sc)
{
        sema_post(&sc->writers_sema);
}

/*
 * Stats cache sx. Protects a small cached statistic.
 */
static __inline void
myfirst_sync_stats_cache_read_begin(struct myfirst_softc *sc)
{
        sx_slock(&sc->stats_cache_sx);
}

static __inline void
myfirst_sync_stats_cache_read_end(struct myfirst_softc *sc)
{
        sx_sunlock(&sc->stats_cache_sx);
}

static __inline int
myfirst_sync_stats_cache_try_promote(struct myfirst_softc *sc)
{
        return (sx_try_upgrade(&sc->stats_cache_sx));
}

static __inline void
myfirst_sync_stats_cache_downgrade(struct myfirst_softc *sc)
{
        sx_downgrade(&sc->stats_cache_sx);
}

static __inline void
myfirst_sync_stats_cache_write_begin(struct myfirst_softc *sc)
{
        sx_xlock(&sc->stats_cache_sx);
}

static __inline void
myfirst_sync_stats_cache_write_end(struct myfirst_softc *sc)
{
        sx_xunlock(&sc->stats_cache_sx);
}

/*
 * Attach-flag atomic operations. Every context that needs to
 * check "are we still attached?" uses these.
 */
static __inline int
myfirst_sync_is_attached(struct myfirst_softc *sc)
{
        return (atomic_load_acq_int(&sc->is_attached));
}

static __inline void
myfirst_sync_mark_detaching(struct myfirst_softc *sc)
{
        atomic_store_rel_int(&sc->is_attached, 0);
}

static __inline void
myfirst_sync_mark_attached(struct myfirst_softc *sc)
{
        atomic_store_rel_int(&sc->is_attached, 1);
}

#endif /* MYFIRST_SYNC_H */
```

The file is under 200 lines including comments. It names every primitive operation. It adds zero runtime overhead. It is the single place a future maintainer looks to understand or change the synchronisation strategy.



## Reference: Extended Lab: The "Break a Primitive" Exercise

An optional lab that stretches Chapter 15's material. For each of the primitives Chapter 15 introduced, deliberately break it and observe the failure.

This is pedagogically valuable because seeing the failure mode makes the correct usage concrete.

### Break the Writer-Cap

In Stage 1, remove one `sema_post` in the write path (say, on the error-return path). Rebuild. Run the writer-cap test. The `writers_sema_value` sysctl should drift downward over time and never recover. Eventually all writers block. This demonstrates why the post must be on every path.

Restore the post. Verify the drift stops and the sema stays balanced under load.

### Break the Sx Upgrade

In Stage 2, remove the re-check after the upgrade fallback:

```c
sx_sunlock(&sc->stats_cache_sx);
sx_xlock(&sc->stats_cache_sx);
/* re-check removed */
myfirst_stats_cache_refresh(sc);
sx_downgrade(&sc->stats_cache_sx);
```

Rebuild. Under heavy reader load, the refresh happens multiple times in quick succession because multiple readers race into the fallback path. The refresh counter grows much faster than one per second.

Restore the re-check. Verify the counter settles back to one refresh per second.

### Break the Partial-Progress Handling

In Stage 3, remove the partial-progress check on the EINTR path:

```c
case EINTR:
case ERESTART:
        return (error);  /* Partial check removed. */
```

Rebuild. Run the signal-handling lab. A SIGINT during a partially-completed read now returns EINTR instead of the partial byte count. User-space code expecting the UNIX convention is surprised.

Restore the check. Verify partial-progress works again.

### Break the Atomic Read

In Stage 4, replace `atomic_load_acq_int(&sc->is_attached)` with a plain read `sc->is_attached` in the read path's entry check. On x86 this still works (strong memory model). On arm64 it may occasionally miss a detach, producing an ENXIO-or-not-ENXIO race.

If you do not have arm64 hardware, this one is hard to demonstrate experimentally. Understand it intellectually and move on.

Restore the atomic. The discipline is the same regardless of test outcome.

### Break the Detach Ordering

Swap the order of `seldrain` and `taskqueue_drain` in Stage 4's detach (as in the earlier debugging scenario). Run the stress test with detach cycles. Observe the eventual panic.

Restore the correct order. Verify stability.

### Break the Sema Destroy Lifetime

Call `sema_destroy(&sc->writers_sema)` early, before the taskqueue is fully drained. On a debug kernel this panics with "sema: waiters" the moment a thread is still inside `sema_wait`. The KASSERT fires.

Restore the correct order. The destroy happens after all waiters have drained.

### Why This Matters

Deliberately breaking code is uncomfortable. It is also the fastest way to internalise why the correct code is written the way it is. Each Chapter 15 primitive has failure modes; seeing them live makes the correct usage unforgettable.

After running the break-and-observe exercise for each primitive, the chapter's material will feel solid. You will know not just what to do, but why, and what happens if you skip a step.



## Reference: When To Split A Driver

A meta-observation that belongs here rather than in a specific section.

Part 3 has developed the `myfirst` driver to a moderate size: about 1200 lines in the main source, plus the `myfirst_sync.h` header, plus the cbuf. That is small for a real driver. Real drivers range from 2000 lines (simple device support) to 30000 or more (network drivers with offload support).

At what size does a driver warrant being split into multiple source files?

A few heuristics.

- **Under 1000 lines**: one file. The overhead of multiple files exceeds the readability benefit.
- **1000 to 5000 lines**: one file is still fine. Use clear section markers in the file.
- **5000 to 15000 lines**: two or three files. Typical split: main attach/detach logic in `foo.c`, dedicated subsystem logic (e.g., a ring buffer manager) in `foo_ring.c`, hardware register definitions in `foo_reg.h`.
- **Over 15000 lines**: modular design is required. A header for shared structures; several implementation files for subsystems; a top-level `foo.c` that ties them together.

The `myfirst` driver at Chapter 15 Stage 4 is comfortable as a single file plus a sync header. As later chapters add hardware-specific logic, a natural split will emerge: `myfirst_reg.h` for register definitions, `myfirst_intr.c` for interrupt-related code, `myfirst_io.c` for the data path. Those splits will happen in Part 4 when the hardware story justifies them.

The rule of thumb: split when a file exceeds what you can mentally hold at once. For most readers that is around 2000 to 5000 lines. Split earlier if the subsystem boundary is natural; split later if the code is so interleaved that a split would feel forced.



## Reference: A Final Summary of Part 3

Part 3 was a walkthrough of five primitives and their composition. A final summary frames what has been accomplished.

**Chapter 11** introduced concurrency into the driver. One user became many users. The mutex made that safe.

**Chapter 12** introduced blocking. Readers and writers could wait for state changes. Condition variables made the wait efficient; sx locks made configuration access scalable.

**Chapter 13** introduced time. Callouts let the driver act on its own at chosen moments. Lock-aware callouts and drain-at-detach made the timers safe.

**Chapter 14** introduced deferred work. Tasks let callouts and other edge contexts hand work off to threads that could actually do the work. Private taskqueues and coalescing made the primitive efficient.

**Chapter 15** introduced the remaining coordination primitives. Semaphores capped concurrency. Refined sx patterns enabled read-mostly caches. Signal-interruptible waits with partial-progress preserved the UNIX conventions. Atomic operations made cross-context flags cheap. Encapsulation made the whole vocabulary readable.

Together, the five chapters built a driver with a complete internal synchronisation story. The driver at `0.9-coordination` has no synchronisation feature missing; every invariant it cares about has a named primitive, a named operation in the wrapper header, a named section in `LOCKING.md`, and a test in the stress kit.

Part 4 adds the hardware story. The synchronisation story stays.



## Reference: Chapter 15 Deliverables Checklist

Before closing Chapter 15, confirm all deliverables are in place.

### Chapter Content

- [ ] `content/chapters/part3/chapter-15.md` exists.
- [ ] Section 1 through Section 8 are written.
- [ ] Additional Topics section is written.
- [ ] Hands-On Labs are written.
- [ ] Challenge Exercises are written.
- [ ] Troubleshooting Reference is written.
- [ ] Wrapping Up Part 3 is written.
- [ ] Bridge to Chapter 16 is written.

### Examples

- [ ] `examples/part-03/ch15-more-synchronization/` directory exists.
- [ ] `stage1-writers-sema/` has a working driver.
- [ ] `stage2-stats-cache/` has a working driver.
- [ ] `stage3-interruptible/` has a working driver.
- [ ] `stage4-final/` has the consolidated driver.
- [ ] Each stage has a `Makefile`.
- [ ] `stage4-final/` has `myfirst_sync.h`.
- [ ] `labs/` has test programs and scripts.
- [ ] `README.md` describes each stage.
- [ ] `LOCKING.md` has the updated synchronisation map.

### Documentation

- [ ] The main source has a top-of-file comment summarising the Chapter 15 additions.
- [ ] Every new sysctl has a description string.
- [ ] Every new structure field has a comment.
- [ ] `LOCKING.md` sections match the driver.

### Testing

- [ ] The stage 4 driver builds cleanly.
- [ ] The stage 4 driver passes WITNESS.
- [ ] The Chapter 11-14 regression tests still pass.
- [ ] The Chapter 15 specific tests pass.
- [ ] Detach under load is clean.

A driver and chapter that pass this checklist are done.



## Reference: An Invitation to Experiment

Before closing, a final invitation.

The Chapter 15 driver is a vehicle for learning, not a shipping product. Every primitive it uses is real; every technique it demonstrates is used in real drivers. But the driver itself is deliberately contrived to exercise the full range of primitives in one place. A real driver typically uses a subset.

As you close Part 3, consider experimenting beyond the chapter's worked examples.

- Add a second type of admission control: a semaphore that caps concurrent readers as well as writers. Does it improve or hurt the system? Why?
- Add a watchdog that times out a single write operation (not the whole driver). Implement it with a timeout task. What edge cases do you encounter?
- Convert the configuration sx to a set of atomic fields. Measure the performance difference with DTrace. Which design would you ship? Why?
- Write a userland test harness in C that exercises the driver in ways the shell can't. What primitives do you reach for?
- Read a real driver in `/usr/src/sys/dev/` and identify a single synchronisation decision it made. Do you agree with the decision? What would you have chosen instead, and why?

Each experiment is a day or two of work. Each teaches more than a chapter of text. The `myfirst` driver is a lab; the FreeBSD source is a library; your own curiosity is the syllabus.

Part 3 has taught you the primitives. The rest is practice. Good luck with Part 4.


## Reference: cv_signal vs cv_broadcast, Precisely

One recurring question when reading or writing driver code: should a signal wake one waiter (cv_signal) or all of them (cv_broadcast)? The answer is not always obvious; this reference digs in.

### The Semantic Difference

`cv_signal(&cv)` wakes at most one thread currently blocked on the cv. If multiple threads are waiting, exactly one of them wakes; the kernel chooses which (usually FIFO, but this is not guaranteed by the API).

`cv_broadcast(&cv)` wakes all threads currently blocked on the cv. Every blocked thread wakes and re-contends for the mutex.

### When `cv_signal` Is Correct

Two conditions must both hold for `cv_signal` to be safe.

**Any one waiter can satisfy the state change.** If you signal because a slot became available in a bounded buffer, and any waiter can take that slot, signal is appropriate. The single wakeup is enough.

**All waiters are equivalent.** If every waiter is running the same predicate and would respond the same way to the wakeup, signal is appropriate. Waking just one avoids the thundering-herd effect of waking all and having all but one immediately re-sleep.

Classic example: a producer/consumer with one item freshly produced. Waking one consumer is sufficient; waking all would wake many consumers who would then see an empty queue and re-sleep.

### When `cv_broadcast` Is Correct

A few specific cases make broadcast the right choice.

**Multiple waiters may succeed.** If a state change unblocks more than one waiter (for example, "the bounded buffer went from full to 10 slots free"), broadcast wakes them all and each one can try. Signaling only one would leave the others blocked even though progress is possible.

**Different waiters have different predicates.** If some waiters are waiting for "bytes > 0" and others for "bytes > 100", signal of one might wake a waiter whose predicate is not satisfied, while another waiter whose predicate is satisfied stays asleep. Broadcast ensures every waiter re-evaluates its own predicate.

**Shutdown or state invalidation.** When the driver detaches, every waiter must see the change and exit. `cv_broadcast` is required because every waiter must return, not just one.

The Chapter 12-15 driver uses `cv_broadcast` on detach (`cv_broadcast(&sc->data_cv)`, `cv_broadcast(&sc->room_cv)`) for exactly this reason. It uses `cv_signal` on normal buffer-state transitions because each transition unblocks at most one waiter productively.

### A Subtle Case: The Reset Sysctl

Chapter 12 added a reset sysctl that clears the cbuf. After reset, the buffer is empty and has full room. Which wakeup is correct?

```c
cv_broadcast(&sc->room_cv);   /* Room is now fully available. */
```

The driver uses `cv_broadcast`. Why not signal? Because the reset unblocked potentially many writers who were all waiting for room. Waking them all lets all of them re-check. A signal would wake only one; the others would stay blocked until a write path signalled on a per-byte basis later.

This is the "multiple waiters may succeed" case. Broadcast is correct.

### Cost Consideration

`cv_broadcast` is more expensive than `cv_signal`. Every woken thread makes the scheduler do work, and every thread that wakes and immediately re-sleeps pays context-switch overhead. For a cv with many waiters, broadcast can be expensive.

For a cv with one or two typical waiters, the cost difference is negligible. Use whichever is semantically correct.

### Rules of Thumb

- **Blocking read wakeup after a byte arrives**: `cv_signal`. One byte can unblock at most one reader.
- **Blocking write wakeup after bytes drain**: depends on how many bytes drained. If one byte drained, signal is fine. If the buffer was emptied by a reset, broadcast.
- **Detach**: always `cv_broadcast`. Every waiter must exit.
- **Reset or state invalidation that could unblock many**: `cv_broadcast`.
- **Normal incremental state change**: `cv_signal`.

When in doubt, `cv_broadcast` is correct (just more expensive). Prefer signal when you can prove it suffices.



## Reference: The Rare Case Where You Write Your Own Semaphore

A thought experiment. If `sema(9)` did not exist, how would you implement a counting semaphore with only a mutex and a cv?

```c
struct my_sema {
        struct mtx      mtx;
        struct cv       cv;
        int             value;
};

static void
my_sema_init(struct my_sema *s, int value, const char *name)
{
        mtx_init(&s->mtx, name, NULL, MTX_DEF);
        cv_init(&s->cv, name);
        s->value = value;
}

static void
my_sema_destroy(struct my_sema *s)
{
        mtx_destroy(&s->mtx);
        cv_destroy(&s->cv);
}

static void
my_sema_wait(struct my_sema *s)
{
        mtx_lock(&s->mtx);
        while (s->value == 0)
                cv_wait(&s->cv, &s->mtx);
        s->value--;
        mtx_unlock(&s->mtx);
}

static void
my_sema_post(struct my_sema *s)
{
        mtx_lock(&s->mtx);
        s->value++;
        cv_signal(&s->cv);
        mtx_unlock(&s->mtx);
}
```

Compact, correct, and functionally identical to `sema(9)` for the simple cases. Reading this makes clear what `sema(9)` is doing internally: exactly this, wrapped in an API.

Why does `sema(9)` exist if it is so simple to write? A few reasons:

- It factors the code out of every driver that would otherwise reinvent it.
- It provides a documented, tested primitive with tracing support.
- It optimises the post to avoid cv_signal when no waiters are present.
- It provides a consistent vocabulary for code review.

The same argument applies to every kernel primitive. You could roll your own mutex, cv, sx, taskqueue, callout. You do not because the kernel's primitives are better-tested, better-documented, and better-understood by the community. Use them.

The exception is a primitive that is not in the kernel. If your driver needs a specific synchronisation idiom that no kernel primitive provides, implementing it is justified. Document it carefully.



## Reference: A Parting Observation on Kernel Synchronization

An observation that spans Part 3.

Every kernel synchronisation primitive is built from simpler primitives. At the bottom is a spinlock (technically, a compare-and-swap on a memory location, plus barriers). Above spinlocks are mutexes (spinlocks plus priority inheritance plus sleeping). Above mutexes are condition variables (sleep queues plus mutex handoff). Above cvs are sx locks (cv plus a reader counter). Above sxs are semaphores (cv plus a counter). Above semaphores are higher-level primitives (taskqueues, gtaskqueues, epochs).

Each layer adds a specific capability and hides the complexity below. When you call `sema_wait`, you do not think about the cv inside it, the mutex inside the cv, the spinlock inside the mutex, the CAS inside the spinlock. The abstraction works.

The payoff of this layering is that you can reason about one layer at a time. The payoff of knowing the layering is that when a layer fails, you can descend to the one below and debug.

Part 3 introduced the primitives at each layer in order. Part 4 uses them. If a Part 4 bug baffles you, the diagnosis might require descending: from "taskqueue is stuck" to "the task callback is blocked on a mutex" to "the mutex is held by a thread waiting on a cv" to "the cv is waiting for a state change that will never happen because of a different bug". The tools for this descent are the primitives you now know.

That is the real payoff of Part 3. Not a specific driver pattern, though that is valuable. Not a specific set of API calls, though those are necessary. The real payoff is a mental model of synchronisation that scales with the complexity of the problem. That model is what will carry you through Part 4 and the rest of the book.



Chapter 15 is complete. Part 3 is complete.

Continue to Chapter 16.

## Reference: A Final Note on Testing Discipline

Every chapter of Part 3 ended with tests. The discipline was consistent: add a primitive, refactor the driver, write a test that exercises it, run the whole regression suite, update `LOCKING.md`.

That discipline is what turns a sequence of chapters into a maintainable body of code. Without it, the driver would be a patchwork of features that work individually and break in combination. With it, every chapter's additions compose with what came before.

Keep the discipline in Part 4. Hardware introduces new primitives (interrupt handlers, resource allocations, DMA tags) and new failure modes (hardware-level races, DMA corruption, register-ordering surprises). Each addition deserves its own test, its own documentation entry, its own integration into the existing regression suite.

The cost of the discipline is a small amount of extra work per chapter. The benefit is that the driver, at whatever stage of development, is always shippable. You can hand it to a colleague, and it will work. You can put it aside for six months, come back, and still understand what it does. You can add one more feature without fearing that something unrelated will break.

That is the ultimate payoff of Part 3. Not just primitives; not just patterns; a working discipline.
